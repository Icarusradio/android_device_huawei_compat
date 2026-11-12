/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth_hci_proxy.h"

#include <hidl/HidlTransportSupport.h>
#include <utils/Log.h>

#include <cstdlib>
#include <string>

int main() {
  using android::hardware::configureRpcThreadpool;
  using android::hardware::joinRpcThreadpool;
  using android::hardware::bluetooth::V1_0::BluetoothHciProxy;
  using android::hardware::bluetooth::V1_0::IBluetoothHci;

  configureRpcThreadpool(5, true);
  const auto implementation =
      IBluetoothHci::getService(std::string("default"), true);
  if (implementation == nullptr || implementation->isRemote()) {
    ALOGE("Could not load local passthrough Bluetooth HCI implementation");
    return EXIT_FAILURE;
  }

  const android::sp<IBluetoothHci> proxy =
      new BluetoothHciProxy(implementation);
  const auto status = proxy->registerAsService("default");
  if (status != android::OK) {
    ALOGE("Could not register Bluetooth HCI compatibility service: %d", status);
    return EXIT_FAILURE;
  }

  joinRpcThreadpool();
  return EXIT_FAILURE;
}
