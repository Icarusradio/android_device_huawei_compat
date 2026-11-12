/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "huawei_bluetooth_compat"

#include "bluetooth_hci_proxy.h"

#include <utils/Log.h>

namespace android::hardware::bluetooth::V1_0 {
namespace {

constexpr uint16_t kReadLocalSupportedFeatures = 0x1003;
constexpr uint16_t kReadLocalExtendedFeatures = 0x1004;
constexpr uint8_t kHciCommandCompleteEvent = 0x0e;
constexpr uint8_t kNonFlushablePacketBoundaryFeatureByte = 6;
constexpr uint8_t kNonFlushablePacketBoundaryFeatureMask = 0x40;

} // namespace

bool maskLocalNonFlushablePbFeature(hidl_vec<uint8_t> *event) {
  // CMR-AL09's stock stack uses flushable PB and host-only L2CAP queue clearing
  // even when its controller advertises bit 54. Masking the local capability
  // selects the unmodified stack's fallback, including its PB selection.
  // The product must opt in to this service; this is not an A2DP-only quirk.
  // HIDL events have no H4 type byte. Only successful local feature responses
  // describe this controller; never change remote features or command support.
  if (event == nullptr || event->size() < 5 ||
      (*event)[0] != kHciCommandCompleteEvent ||
      static_cast<size_t>((*event)[1]) + 2 != event->size()) {
    return false;
  }

  const uint8_t parameter_length = (*event)[1];
  const uint16_t opcode = static_cast<uint16_t>((*event)[3]) |
                          (static_cast<uint16_t>((*event)[4]) << 8);
  size_t feature_offset;
  if (opcode == kReadLocalSupportedFeatures) {
    if (parameter_length != 12)
      return false;
    feature_offset = 6;
  } else if (opcode == kReadLocalExtendedFeatures) {
    if (parameter_length != 14 || (*event)[6] != 0)
      return false;
    feature_offset = 8;
  } else {
    return false;
  }

  if ((*event)[5] != 0)
    return false;
  const size_t feature_byte =
      feature_offset + kNonFlushablePacketBoundaryFeatureByte;
  if (((*event)[feature_byte] & kNonFlushablePacketBoundaryFeatureMask) == 0)
    return false;

  (*event)[feature_byte] &=
      static_cast<uint8_t>(~kNonFlushablePacketBoundaryFeatureMask);
  return true;
}

FilteredCallbacks::FilteredCallbacks(const sp<IBluetoothHciCallbacks> &callback)
    : callback_(callback) {}

Return<void> FilteredCallbacks::initializationComplete(Status status) {
  return callback_->initializationComplete(status);
}

Return<void>
FilteredCallbacks::hciEventReceived(const hidl_vec<uint8_t> &event) {
  if (event.size() != 0) {
    hidl_vec<uint8_t> forwarded = event;
    if (maskLocalNonFlushablePbFeature(&forwarded)) {
      ALOGI("Masking local Non-Flushable PB feature for controller "
            "compatibility");
      return callback_->hciEventReceived(forwarded);
    }
  }
  return callback_->hciEventReceived(event);
}

Return<void> FilteredCallbacks::aclDataReceived(const hidl_vec<uint8_t> &data) {
  return callback_->aclDataReceived(data);
}

Return<void> FilteredCallbacks::scoDataReceived(const hidl_vec<uint8_t> &data) {
  return callback_->scoDataReceived(data);
}

Return<bool>
FilteredCallbacks::linkToDeath(const sp<hidl_death_recipient> &recipient,
                               uint64_t cookie) {
  // The original HAL owns the death recipient and closes its transport when
  // the real Bluetooth client dies. A local callback must forward this link.
  return callback_->linkToDeath(recipient, cookie);
}

Return<bool>
FilteredCallbacks::unlinkToDeath(const sp<hidl_death_recipient> &recipient) {
  return callback_->unlinkToDeath(recipient);
}

BluetoothHciProxy::BluetoothHciProxy(const sp<IBluetoothHci> &implementation)
    : implementation_(implementation) {}

Return<void>
BluetoothHciProxy::initialize(const sp<IBluetoothHciCallbacks> &callback) {
  if (callback == nullptr)
    return implementation_->initialize(callback);
  return implementation_->initialize(new FilteredCallbacks(callback));
}

Return<void>
BluetoothHciProxy::sendHciCommand(const hidl_vec<uint8_t> &command) {
  return implementation_->sendHciCommand(command);
}

Return<void> BluetoothHciProxy::sendAclData(const hidl_vec<uint8_t> &data) {
  return implementation_->sendAclData(data);
}

Return<void> BluetoothHciProxy::sendScoData(const hidl_vec<uint8_t> &data) {
  return implementation_->sendScoData(data);
}

Return<void> BluetoothHciProxy::close() { return implementation_->close(); }

} // namespace android::hardware::bluetooth::V1_0
