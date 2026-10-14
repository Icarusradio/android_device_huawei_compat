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

#pragma once

#include <hidl/HidlSupport.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "async_fd_watcher.h"
#include "bt_vendor_lib.h"
#include "hci_protocol.h"

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_vec;
using InitializeCompleteCallback = std::function<void(bool success)>;
using PacketReadCallback = std::function<void(const hidl_vec<uint8_t>&)>;

class FirmwareStartupTimer;

class VendorInterface {
 public:
  // A token is never reused, including after a failed initialization.
  struct Session {
    Session(uint64_t id, pid_t client_pid) : id(id), client_pid(client_pid) {}
    const uint64_t id;
    const pid_t client_pid;
    std::atomic_bool cancelled{false};
  };
  using SessionPtr = std::shared_ptr<Session>;

  static SessionPtr NewSession(pid_t client_pid);
  static void Initialize(SessionPtr session,
                         InitializeCompleteCallback initialize_complete_cb,
                         PacketReadCallback event_cb, PacketReadCallback acl_cb,
                         PacketReadCallback sco_cb, PacketReadCallback iso_cb);
  static void Shutdown(SessionPtr session);
  static size_t Send(SessionPtr session, uint8_t type, const uint8_t* data,
                     size_t length);
  static uint8_t SendVendorCommand(uint16_t opcode, void* buffer,
                                   tINT_CMD_CBACK callback);
  static void FirmwareConfigured(uint8_t result);
#ifdef BT_FUZZER
  // The in-process fake must not reuse its state or close FDs before queued
  // lifecycle work finishes. Never wait this way from a production callback.
  static void WaitForIdleForTesting();
#endif

 private:
  virtual ~VendorInterface() = default;
  class Lease;

  explicit VendorInterface(SessionPtr session) : session_(session) {}
  static void CloseSession(SessionPtr session);

  bool Open(InitializeCompleteCallback initialize_complete_cb,
            PacketReadCallback event_cb, PacketReadCallback acl_cb,
            PacketReadCallback sco_cb, PacketReadCallback iso_cb);
  void Close();
  size_t SendPacket(uint8_t type, const uint8_t* data, size_t length);
  void OnFirmwareConfigured(uint8_t result);
  void CompleteInitialization(bool success);

  void OnTimeout();

  void HandleIncomingEvent(const hidl_vec<uint8_t>& hci_packet);

  void* lib_handle_ = nullptr;
  bt_vendor_interface_t* lib_interface_ = nullptr;
  bool vendor_init_attempted_ = false;
  bool vendor_initialized_ = false;
  std::atomic_bool transport_ready_{false};
  SessionPtr session_;
  async::AsyncFdWatcher fd_watcher_;
  std::mutex initialization_mutex_;
  InitializeCompleteCallback initialize_complete_cb_;
  hci::HciProtocol* hci_ = nullptr;

  PacketReadCallback event_cb_;

  FirmwareStartupTimer* firmware_startup_timer_ = nullptr;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
