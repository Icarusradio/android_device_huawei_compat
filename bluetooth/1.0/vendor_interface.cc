//
// Copyright 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "vendor_interface.h"

#define LOG_TAG "android.hardware.bluetooth@1.0-impl"
#include <cutils/properties.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <utils/Log.h>

#include <condition_variable>
#include <deque>
#include <thread>
#ifdef BT_FUZZER
#include <future>
#endif

#include "bluetooth_address.h"
#include "h4_protocol.h"
#include "mct_protocol.h"

#ifdef BT_FUZZER
static const char* VENDOR_LIBRARY_NAME = "libbt-vendor-huawei-fuzz.so";
#else
static const char* VENDOR_LIBRARY_NAME = "libbt-vendor.so";
#endif
static const char* VENDOR_LIBRARY_SYMBOL_NAME =
    "BLUETOOTH_VENDOR_LIB_INTERFACE";

static const int INVALID_FD = -1;

namespace {

using android::hardware::hidl_vec;
using android::hardware::bluetooth::V1_0::implementation::VendorInterface;

struct {
  tINT_CMD_CBACK cb;
  uint16_t opcode;
} internal_command;

// True when LPM is not enabled yet or wake is not asserted.
bool lpm_wake_deasserted;
uint32_t lpm_timeout_ms;
bool recent_activity_flag;

VendorInterface* g_vendor_interface = nullptr;
std::recursive_mutex wakeup_mutex_;
std::mutex command_mutex;
std::mutex lifecycle_mutex;
std::condition_variable lifecycle_cv;
size_t active_operations = 0;
bool closing = false;

// Open/Close run on one process-lifetime worker. Binder and vendor callbacks
// may request shutdown without waiting for themselves or the RX thread to exit.
class LifecycleWorker {
 public:
  LifecycleWorker() {
    std::thread([this] { Run(); }).detach();
  }

  void Post(std::function<void()> task,
            VendorInterface::SessionPtr cancel = nullptr) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // Cancellation and queue insertion must be atomic with respect to a new
      // Initialize job. Otherwise a death notification could cancel the old
      // token, get preempted, and let the new Open overtake its Close.
      if (cancel && cancel->cancelled.exchange(true)) return;
      tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
  }

 private:
  void Run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty(); });
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
};

LifecycleWorker& lifecycle_worker() {
  static auto* worker = new LifecycleWorker();
  return *worker;
}

// A stuck vendor call must never lead to unloading executing code. Let init
// restart the HAL process instead. This also bounds a blocked RX/Binder
// callback.
class CloseWatchdog {
 public:
  explicit CloseWatchdog(uint64_t session)
      : thread_([this, session] {
          std::unique_lock<std::mutex> lock(mutex_);
          if (!cv_.wait_for(lock, std::chrono::seconds(10),
                            [this] { return done_; })) {
            LOG_ALWAYS_FATAL(
                "Bluetooth vendor close timed out, session=%" PRIu64, session);
          }
        }) {}

  ~CloseWatchdog() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      done_ = true;
    }
    cv_.notify_one();
    thread_.join();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  std::thread thread_;
};

HC_BT_HDR* WrapPacketAndCopy(uint16_t event, const hidl_vec<uint8_t>& data) {
  size_t packet_size = data.size() + sizeof(HC_BT_HDR);
  HC_BT_HDR* packet = reinterpret_cast<HC_BT_HDR*>(new uint8_t[packet_size]);
  packet->offset = 0;
  packet->len = data.size();
  packet->layer_specific = 0;
  packet->event = event;
  // TODO(eisenbach): Avoid copy here; if BT_HDR->data can be ensured to
  // be the only way the data is accessed, a pointer could be passed here...
  memcpy(packet->data, data.data(), data.size());
  return packet;
}

bool internal_command_event_match(const hidl_vec<uint8_t>& packet) {
  if (packet.size() < HCI_EVENT_PREAMBLE_SIZE + 3) return false;
  uint8_t event_code = packet[0];
  if (event_code != HCI_COMMAND_COMPLETE_EVENT) {
    ALOGE("%s: Unhandled event type %02X", __func__, event_code);
    return false;
  }

  size_t opcode_offset = HCI_EVENT_PREAMBLE_SIZE + 1;  // Skip num packets.

  uint16_t opcode = packet[opcode_offset] | (packet[opcode_offset + 1] << 8);

  ALOGV("%s internal_command.opcode = %04X opcode = %04x", __func__,
        internal_command.opcode, opcode);
  return opcode == internal_command.opcode;
}

uint8_t transmit_cb(uint16_t opcode, void* buffer, tINT_CMD_CBACK callback) {
  ALOGV("%s opcode: 0x%04x, ptr: %p, cb: %p", __func__, opcode, buffer,
        callback);
  return VendorInterface::SendVendorCommand(opcode, buffer, callback);
}

void firmware_config_cb(bt_vendor_op_result_t result) {
  ALOGV("%s result: %d", __func__, result);
  VendorInterface::FirmwareConfigured(result);
}

void sco_config_cb(bt_vendor_op_result_t result) {
  ALOGD("%s result: %d", __func__, result);
}

void low_power_mode_cb(bt_vendor_op_result_t result) {
  ALOGD("%s result: %d", __func__, result);
}

void sco_audiostate_cb(bt_vendor_op_result_t result) {
  ALOGD("%s result: %d", __func__, result);
}

void* buffer_alloc_cb(int size) {
  void* p = new uint8_t[size];
  ALOGV("%s pts: %p, size: %d", __func__, p, size);
  return p;
}

void buffer_free_cb(void* buffer) {
  ALOGV("%s ptr: %p", __func__, buffer);
  delete[] reinterpret_cast<uint8_t*>(buffer);
}

void epilog_cb(bt_vendor_op_result_t result) {
  ALOGD("%s result: %d", __func__, result);
}

void a2dp_offload_cb(bt_vendor_op_result_t result, bt_vendor_opcode_t op,
                     uint8_t av_handle) {
  ALOGD("%s result: %d, op: %d, handle: %d", __func__, result, op, av_handle);
}

const bt_vendor_callbacks_t lib_callbacks = {
    sizeof(lib_callbacks), firmware_config_cb, sco_config_cb,
    low_power_mode_cb,     sco_audiostate_cb,  buffer_alloc_cb,
    buffer_free_cb,        transmit_cb,        epilog_cb,
    a2dp_offload_cb};

}  // namespace

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_0 {
namespace implementation {

class FirmwareStartupTimer {
 public:
  FirmwareStartupTimer() : start_time_(std::chrono::steady_clock::now()) {}

  ~FirmwareStartupTimer() {
    std::chrono::duration<double> duration =
        std::chrono::steady_clock::now() - start_time_;
    double s = duration.count();
    if (s == 0) return;
    ALOGI("Firmware configured in %.3fs", s);
  }

 private:
  std::chrono::steady_clock::time_point start_time_;
};

// An operation pins the current generation until it leaves vendor/transport
// code. Admission and shutdown share a short lock, never a Binder/vendor lock.
class VendorInterface::Lease {
 public:
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  explicit Lease(SessionPtr session = nullptr) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    if (!g_vendor_interface || closing ||
        g_vendor_interface->session_->cancelled ||
        (session && session != g_vendor_interface->session_))
      return;
    vendor_ = g_vendor_interface;
    ++active_operations;
  }

  ~Lease() {
    if (!vendor_) return;
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    --active_operations;
    lifecycle_cv.notify_all();
  }

  explicit operator bool() const { return vendor_ != nullptr; }
  VendorInterface* operator->() const { return vendor_; }

 private:
  VendorInterface* vendor_ = nullptr;
};

VendorInterface::SessionPtr VendorInterface::NewSession(pid_t client_pid) {
  static std::atomic<uint64_t> next_session{1};
  return std::make_shared<Session>(next_session.fetch_add(1), client_pid);
}

void VendorInterface::Initialize(
    SessionPtr session, InitializeCompleteCallback initialize_complete_cb,
    PacketReadCallback event_cb, PacketReadCallback acl_cb,
    PacketReadCallback sco_cb, PacketReadCallback iso_cb) {
  lifecycle_worker().Post(
      [session, initialize_complete_cb, event_cb, acl_cb, sco_cb, iso_cb] {
        if (session->cancelled) return;
        VendorInterface* vendor = nullptr;
        {
          std::lock_guard<std::mutex> lock(lifecycle_mutex);
          if (!g_vendor_interface) {
            vendor = new VendorInterface(session);
            g_vendor_interface = vendor;
            closing = false;
          }
        }
        if (!vendor) {
          ALOGE("Initialize rejected while another session is active: %" PRIu64,
                session->id);
          if (!session->cancelled) initialize_complete_cb(false);
          return;
        }
        ALOGI("Vendor opening, session=%" PRIu64, session->id);
        {
          std::lock_guard<std::mutex> lock(command_mutex);
          internal_command = {};
        }
        if (!vendor->Open(initialize_complete_cb, event_cb, acl_cb, sco_cb,
                          iso_cb)) {
          vendor->CompleteInitialization(false);
          session->cancelled = true;
          CloseSession(session);
        } else if (session->cancelled) {
          CloseSession(session);
        }
      });
}

void VendorInterface::Shutdown(SessionPtr session) {
  if (!session) return;
  // Stop admission before the worker runs and queue one close per token.
  lifecycle_worker().Post([session] { CloseSession(session); }, session);
}

#ifdef BT_FUZZER
void VendorInterface::WaitForIdleForTesting() {
  auto done = std::make_shared<std::promise<void>>();
  auto future = done->get_future();
  lifecycle_worker().Post([done] { done->set_value(); });
  future.wait();
}
#endif

void VendorInterface::CloseSession(SessionPtr session) {
  VendorInterface* vendor;
  {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    if (!g_vendor_interface || g_vendor_interface->session_ != session) return;
    closing = true;
    vendor = g_vendor_interface;
    ALOGI("Vendor closing, session=%" PRIu64 ", active=%zu", session->id,
          active_operations);
  }
  CloseWatchdog watchdog(session->id);
  {
    std::unique_lock<std::mutex> lock(lifecycle_mutex);
    lifecycle_cv.wait(lock, [] { return active_operations == 0; });
  }
  vendor->Close();  // Includes joining RX and vendor cleanup before dlclose.
  {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    g_vendor_interface = nullptr;
  }
  delete vendor;
  ALOGI("Vendor closed, session=%" PRIu64, session->id);
}

size_t VendorInterface::Send(SessionPtr session, uint8_t type,
                             const uint8_t* data, size_t length) {
  if (!session) return 0;
  Lease lease(session);
  return lease ? lease->SendPacket(type, data, length) : 0;
}

uint8_t VendorInterface::SendVendorCommand(uint16_t opcode, void* buffer,
                                           tINT_CMD_CBACK callback) {
  Lease lease;
  bool sent = false;
  if (lease && lease->transport_ready_) {
    {
      std::lock_guard<std::mutex> lock(command_mutex);
      internal_command.cb = callback;
      internal_command.opcode = opcode;
    }
    HC_BT_HDR* bt_hdr = reinterpret_cast<HC_BT_HDR*>(buffer);
    sent = lease->SendPacket(HCI_PACKET_TYPE_COMMAND, bt_hdr->data,
                             bt_hdr->len) != 0;
  }
  delete[] reinterpret_cast<uint8_t*>(buffer);
  return sent;
}

void VendorInterface::FirmwareConfigured(uint8_t result) {
  Lease lease;
  if (lease) lease->OnFirmwareConfigured(result);
}

bool VendorInterface::Open(InitializeCompleteCallback initialize_complete_cb,
                           PacketReadCallback event_cb,
                           PacketReadCallback acl_cb,
                           PacketReadCallback sco_cb,
                           PacketReadCallback iso_cb) {
  initialize_complete_cb_ = initialize_complete_cb;

  // Initialize vendor interface

  lib_handle_ = dlopen(VENDOR_LIBRARY_NAME, RTLD_NOW);
  if (!lib_handle_) {
    ALOGE("%s unable to open %s (%s)", __func__, VENDOR_LIBRARY_NAME,
          dlerror());
    return false;
  }

  lib_interface_ = reinterpret_cast<bt_vendor_interface_t*>(
      dlsym(lib_handle_, VENDOR_LIBRARY_SYMBOL_NAME));
  if (!lib_interface_) {
    ALOGE("%s unable to find symbol %s in %s (%s)", __func__,
          VENDOR_LIBRARY_SYMBOL_NAME, VENDOR_LIBRARY_NAME, dlerror());
    return false;
  }

  // Get the local BD address

  uint8_t local_bda[BluetoothAddress::kBytes];
  if (!BluetoothAddress::get_local_address(local_bda)) {
    LOG_ALWAYS_FATAL("%s: No Bluetooth Address!", __func__);
  }
  vendor_init_attempted_ = true;
  int status = lib_interface_->init(&lib_callbacks, (unsigned char*)local_bda);
  if (status) {
    ALOGE("%s unable to initialize vendor library: %d", __func__, status);
    return false;
  }

  vendor_initialized_ = true;
  ALOGD("%s vendor library loaded", __func__);

  // Power on the controller

  int power_state = BT_VND_PWR_ON;
  lib_interface_->op(BT_VND_OP_POWER_CTRL, &power_state);

  // Get the UART socket(s)

  int fd_list[CH_MAX] = {0};
  int fd_count = lib_interface_->op(BT_VND_OP_USERIAL_OPEN, &fd_list);

  if (fd_count < 1 || fd_count > CH_MAX - 1) {
    ALOGE("%s: fd_count %d is invalid!", __func__, fd_count);
    return false;
  }

  for (int i = 0; i < fd_count; i++) {
    if (fd_list[i] == INVALID_FD) {
      ALOGE("%s: fd %d is invalid!", __func__, fd_list[i]);
      return false;
    }
  }

  event_cb_ = event_cb;
  // Reset power management before publishing a usable transport.
  {
    std::lock_guard<std::recursive_mutex> lock(wakeup_mutex_);
    lpm_wake_deasserted = true;
    lpm_timeout_ms = 0;
    recent_activity_flag = false;
  }
  PacketReadCallback intercept_events = [this](const hidl_vec<uint8_t>& event) {
    HandleIncomingEvent(event);
  };

  if (fd_count == 1) {
    hci::H4Protocol* h4_hci = new hci::H4Protocol(fd_list[0], intercept_events,
                                                  acl_cb, sco_cb, iso_cb);
    hci_ = h4_hci;
    transport_ready_ = true;
    fd_watcher_.WatchFdForNonBlockingReads(fd_list[0], [this, h4_hci](int fd) {
      Lease lease(session_);
      if (lease) h4_hci->OnDataReady(fd);
    });
  } else {
    hci::MctProtocol* mct_hci =
        new hci::MctProtocol(fd_list, intercept_events, acl_cb);
    hci_ = mct_hci;
    transport_ready_ = true;
    fd_watcher_.WatchFdForNonBlockingReads(fd_list[CH_EVT],
                                           [this, mct_hci](int fd) {
                                             Lease lease(session_);
                                             if (lease)
                                               mct_hci->OnEventDataReady(fd);
                                           });
    fd_watcher_.WatchFdForNonBlockingReads(fd_list[CH_ACL_IN],
                                           [this, mct_hci](int fd) {
                                             Lease lease(session_);
                                             if (lease)
                                               mct_hci->OnAclDataReady(fd);
                                           });
  }

  // Start configuring the firmware
  firmware_startup_timer_ = new FirmwareStartupTimer();
  lib_interface_->op(BT_VND_OP_FW_CFG, nullptr);

  return true;
}

void VendorInterface::Close() {
  // These callbacks may send HCI events (vendor-dependent), so make sure to
  // StopWatching the file descriptor after this.
  if (vendor_initialized_) {
    bt_vendor_lpm_mode_t mode = BT_VND_LPM_DISABLE;
    lib_interface_->op(BT_VND_OP_LPM_SET_MODE, &mode);
  }

  fd_watcher_.StopWatchingFileDescriptors();
  transport_ready_ = false;

  if (hci_ != nullptr) {
    delete hci_;
    hci_ = nullptr;
  }

  if (vendor_initialized_) {
    ALOGI("USERIAL_CLOSE begin, session=%" PRIu64, session_->id);
    lib_interface_->op(BT_VND_OP_USERIAL_CLOSE, nullptr);
    ALOGI("USERIAL_CLOSE end, session=%" PRIu64, session_->id);

    int power_state = BT_VND_PWR_OFF;
    lib_interface_->op(BT_VND_OP_POWER_CTRL, &power_state);

    vendor_initialized_ = false;
  }

  // Even a failed init may have installed callbacks or allocated resources.
  if (vendor_init_attempted_) {
    lib_interface_->cleanup();
    vendor_init_attempted_ = false;
  }

  lib_interface_ = nullptr;
  if (lib_handle_ != nullptr) {
    ALOGI("Vendor dlclose, session=%" PRIu64, session_->id);
    dlclose(lib_handle_);
    lib_handle_ = nullptr;
  }

  if (firmware_startup_timer_ != nullptr) {
    delete firmware_startup_timer_;
    firmware_startup_timer_ = nullptr;
  }
}

size_t VendorInterface::SendPacket(uint8_t type, const uint8_t* data,
                                   size_t length) {
  if (!transport_ready_ || length == 0) return 0;
  std::unique_lock<std::recursive_mutex> lock(wakeup_mutex_);
  recent_activity_flag = true;

  if (lpm_wake_deasserted == true) {
    // Restart the timer.
    fd_watcher_.ConfigureTimeout(std::chrono::milliseconds(lpm_timeout_ms),
                                 [this]() {
                                   Lease lease(session_);
                                   if (lease) OnTimeout();
                                 });
    // Assert wake.
    lpm_wake_deasserted = false;
    bt_vendor_lpm_wake_state_t wakeState = BT_VND_LPM_WAKE_ASSERT;
    lib_interface_->op(BT_VND_OP_LPM_WAKE_SET_STATE, &wakeState);
    ALOGV("%s: Sent wake before packet type %02x", __func__, type);
  }

  return hci_->Send(type, data, length);
}

void VendorInterface::CompleteInitialization(bool success) {
  InitializeCompleteCallback callback;
  {
    std::lock_guard<std::mutex> lock(initialization_mutex_);
    callback = std::move(initialize_complete_cb_);
    initialize_complete_cb_ = nullptr;
    delete firmware_startup_timer_;
    firmware_startup_timer_ = nullptr;
  }
  if (callback && !session_->cancelled) callback(success);
}

void VendorInterface::OnFirmwareConfigured(uint8_t result) {
  ALOGD("%s result: %d, session=%" PRIu64, __func__, result, session_->id);
  if (result == 0 && transport_ready_) {
    std::lock_guard<std::recursive_mutex> lock(wakeup_mutex_);
    lib_interface_->op(BT_VND_OP_GET_LPM_IDLE_TIMEOUT, &lpm_timeout_ms);
    ALOGI("%s: lpm_timeout_ms %d", __func__, lpm_timeout_ms);

    bt_vendor_lpm_mode_t mode = BT_VND_LPM_ENABLE;
    lib_interface_->op(BT_VND_OP_LPM_SET_MODE, &mode);
    fd_watcher_.ConfigureTimeout(std::chrono::milliseconds(lpm_timeout_ms),
                                 [this] {
                                   Lease lease(session_);
                                   if (lease) OnTimeout();
                                 });
  }
  CompleteInitialization(result == 0);
  if (result != 0) Shutdown(session_);
}

void VendorInterface::OnTimeout() {
  ALOGV("%s", __func__);
  std::unique_lock<std::recursive_mutex> lock(wakeup_mutex_);
  if (recent_activity_flag == false) {
    lpm_wake_deasserted = true;
    bt_vendor_lpm_wake_state_t wakeState = BT_VND_LPM_WAKE_DEASSERT;
    lib_interface_->op(BT_VND_OP_LPM_WAKE_SET_STATE, &wakeState);
    fd_watcher_.ConfigureTimeout(std::chrono::seconds(0), []() {
      ALOGE("Zero timeout! Should never happen.");
    });
  }
  recent_activity_flag = false;
}

void VendorInterface::HandleIncomingEvent(const hidl_vec<uint8_t>& hci_packet) {
  tINT_CMD_CBACK saved_cb = nullptr;
  {
    std::lock_guard<std::mutex> lock(command_mutex);
    if (internal_command.cb != nullptr &&
        internal_command_event_match(hci_packet)) {
      saved_cb = internal_command.cb;
      internal_command.cb = nullptr;
    }
  }
  // Vendor callbacks can synchronously send another command.
  if (saved_cb) {
    saved_cb(WrapPacketAndCopy(HCI_PACKET_TYPE_EVENT, hci_packet));
  } else if (!session_->cancelled) {
    event_cb_(hci_packet);
  }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
