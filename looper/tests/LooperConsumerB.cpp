/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/Looper.h>

extern "C" android::Looper* looperGetFromConsumerB() {
    android::sp<android::Looper> looper = android::Looper::getForThread();
    return looper.get();
}
