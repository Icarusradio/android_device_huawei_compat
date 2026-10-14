/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.bluetooth@1.0-service.huawei"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "bluetooth_hci.h"

using android::hardware::bluetooth::V1_0::implementation::BluetoothHci;

int main() {
  // Match the platform 1.0 service's Binder thread pool, without loading the
  // platform passthrough implementation in place of the Huawei implementation.
  android::hardware::configureRpcThreadpool(5, true);
  android::sp<BluetoothHci> bluetooth_hci = new BluetoothHci();
  if (bluetooth_hci->registerAsService("default") != android::OK) {
    ALOGE("Cannot register Huawei Bluetooth HAL service");
    return 1;
  }
  android::hardware::joinRpcThreadpool();
  return 1;
}
