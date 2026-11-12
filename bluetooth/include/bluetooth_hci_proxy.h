/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/hardware/bluetooth/1.0/IBluetoothHci.h>
#include <android/hardware/bluetooth/1.0/IBluetoothHciCallbacks.h>

namespace android::hardware::bluetooth::V1_0 {

bool maskLocalNonFlushablePbFeature(hidl_vec<uint8_t> *event);

class FilteredCallbacks final : public IBluetoothHciCallbacks {
public:
  explicit FilteredCallbacks(const sp<IBluetoothHciCallbacks> &callback);

  Return<void> initializationComplete(Status status) override;
  Return<void> hciEventReceived(const hidl_vec<uint8_t> &event) override;
  Return<void> aclDataReceived(const hidl_vec<uint8_t> &data) override;
  Return<void> scoDataReceived(const hidl_vec<uint8_t> &data) override;
  Return<bool> linkToDeath(const sp<hidl_death_recipient> &recipient,
                           uint64_t cookie) override;
  Return<bool>
  unlinkToDeath(const sp<hidl_death_recipient> &recipient) override;

private:
  const sp<IBluetoothHciCallbacks> callback_;
};

class BluetoothHciProxy final : public IBluetoothHci {
public:
  explicit BluetoothHciProxy(const sp<IBluetoothHci> &implementation);

  Return<void> initialize(const sp<IBluetoothHciCallbacks> &callback) override;
  Return<void> sendHciCommand(const hidl_vec<uint8_t> &command) override;
  Return<void> sendAclData(const hidl_vec<uint8_t> &data) override;
  Return<void> sendScoData(const hidl_vec<uint8_t> &data) override;
  Return<void> close() override;

private:
  const sp<IBluetoothHci> implementation_;
};

} // namespace android::hardware::bluetooth::V1_0
