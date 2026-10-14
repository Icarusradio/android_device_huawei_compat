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

#define LOG_TAG "android.hardware.bluetooth@1.0-impl"
#include "bluetooth_hci.h"

#include <hwbinder/IPCThreadState.h>
#include <log/log.h>

#include "vendor_interface.h"

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_0 {
namespace implementation {

static const uint8_t HCI_DATA_TYPE_COMMAND = 1;
static const uint8_t HCI_DATA_TYPE_ACL = 2;
static const uint8_t HCI_DATA_TYPE_SCO = 3;

class BluetoothDeathRecipient : public hidl_death_recipient {
 public:
  BluetoothDeathRecipient(BluetoothHci* hci,
                          VendorInterface::SessionPtr session)
      : hci_(hci), session_(session) {}

  void serviceDied(
      uint64_t /*cookie*/,
      const wp<::android::hidl::base::V1_0::IBase>& /*who*/) override {
    ALOGE("Bluetooth service died");
    has_died_ = true;
    // Even a notification delivered after unlink belongs only to this token.
    VendorInterface::Shutdown(session_);
    auto hci = hci_.promote();
    if (hci) hci->closeSession(session_);
  }

  bool getHasDied() const { return has_died_; }

 private:
  wp<BluetoothHci> hci_;
  VendorInterface::SessionPtr session_;
  std::atomic_bool has_died_{false};
};

BluetoothHci::BluetoothHci() = default;

Return<void> BluetoothHci::initialize(
    const ::android::sp<IBluetoothHciCallbacks>& cb) {
  ALOGI("BluetoothHci::initialize()");
  if (cb == nullptr) {
    ALOGE("cb == nullptr! -> Unable to call initializationComplete(ERR)");
    return Void();
  }

  auto session =
      VendorInterface::NewSession(IPCThreadState::self()->getCallingPid());
  sp<BluetoothDeathRecipient> recipient =
      new BluetoothDeathRecipient(this, session);
  std::function<void()> old_unlink;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    VendorInterface::Shutdown(session_);
    old_unlink = std::move(unlink_cb_);
    session_ = session;
    death_recipient_ = recipient;
    unlink_cb_ = [cb, recipient] {
      if (!recipient->getHasDied()) {
        auto status = cb->unlinkToDeath(recipient);
        if (!status.isOk()) ALOGW("Unable to unlink Bluetooth death recipient");
      }
    };
  }
  if (old_unlink) old_unlink();

  auto linked = cb->linkToDeath(recipient, session->id);
  if (!linked.isOk() || (cb->isRemote() && !static_cast<bool>(linked))) {
    closeSession(session);
    auto status = cb->initializationComplete(Status::INITIALIZATION_ERROR);
    if (!status.isOk())
      ALOGE("Unable to report death-link initialization failure");
    return Void();
  }

  // close() may have run before linkToDeath returned. Remove that late link.
  if (session->cancelled) {
    auto status = cb->unlinkToDeath(recipient);
    if (!status.isOk()) ALOGW("Unable to unlink cancelled Bluetooth session");
    return Void();
  }
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (session_ != session || session->cancelled) return Void();
  VendorInterface::Initialize(
      session,
      [cb](bool status) {
        auto hidl_status = cb->initializationComplete(
            status ? Status::SUCCESS : Status::INITIALIZATION_ERROR);
        if (!hidl_status.isOk()) {
          ALOGE("VendorInterface -> Unable to call initializationComplete()");
        }
      },
      [cb](const hidl_vec<uint8_t>& packet) {
        auto hidl_status = cb->hciEventReceived(packet);
        if (!hidl_status.isOk()) {
          ALOGE("VendorInterface -> Unable to call hciEventReceived()");
        }
      },
      [cb](const hidl_vec<uint8_t>& packet) {
        auto hidl_status = cb->aclDataReceived(packet);
        if (!hidl_status.isOk()) {
          ALOGE("VendorInterface -> Unable to call aclDataReceived()");
        }
      },
      [cb](const hidl_vec<uint8_t>& packet) {
        auto hidl_status = cb->scoDataReceived(packet);
        if (!hidl_status.isOk()) {
          ALOGE("VendorInterface -> Unable to call scoDataReceived()");
        }
      },
      [cb](const hidl_vec<uint8_t>&) {
        ALOGE("VendorInterface -> No callback for ISO packets in HAL V1_0");
      });

  return Void();
}

Return<void> BluetoothHci::close() {
  SessionPtr session;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session = session_;
    // The HIDL API has no close/send token. Reject calls from an old process
    // after a replacement client has initialized a new session.
    if (session &&
        session->client_pid != IPCThreadState::self()->getCallingPid()) {
      ALOGW("Ignoring Bluetooth request from a previous client process");
      return Void();
    }
  }
  closeSession(session);
  return Void();
}

void BluetoothHci::closeSession(SessionPtr session) {
  std::function<void()> unlink;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    VendorInterface::Shutdown(session);
    if (!session || session_ != session) return;
    session_.reset();
    death_recipient_.clear();
    unlink = std::move(unlink_cb_);
  }
  if (unlink) unlink();
}

Return<void> BluetoothHci::sendHciCommand(const hidl_vec<uint8_t>& command) {
  sendDataToController(HCI_DATA_TYPE_COMMAND, command);
  return Void();
}

Return<void> BluetoothHci::sendAclData(const hidl_vec<uint8_t>& data) {
  sendDataToController(HCI_DATA_TYPE_ACL, data);
  return Void();
}

Return<void> BluetoothHci::sendScoData(const hidl_vec<uint8_t>& data) {
  sendDataToController(HCI_DATA_TYPE_SCO, data);
  return Void();
}

void BluetoothHci::sendDataToController(const uint8_t type,
                                        const hidl_vec<uint8_t>& data) {
  SessionPtr session;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session = session_;
    if (session &&
        session->client_pid != IPCThreadState::self()->getCallingPid()) {
      ALOGW("Ignoring Bluetooth send from a previous client process");
      return;
    }
  }
  VendorInterface::Send(session, type, data.data(), data.size());
}

IBluetoothHci* HIDL_FETCH_IBluetoothHci(const char* /* name */) {
  return new BluetoothHci();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
