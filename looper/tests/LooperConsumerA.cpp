/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/Looper.h>

extern "C" android::Looper* looperPrepareFromConsumerA() {
    android::sp<android::Looper> looper = android::Looper::prepare(0);
    return looper.get();
}
