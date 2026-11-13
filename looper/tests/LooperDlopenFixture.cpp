/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>

#include <atomic>

#include <utils/Looper.h>

extern "C" android::Looper* looperPrepareFromConsumerA();
extern "C" android::Looper* looperGetFromConsumerB();

namespace {

std::atomic<int> gResult{-1};

void* prepareLooperOnWorker(void*) {
    android::Looper* const prepared = looperPrepareFromConsumerA();
    if (prepared == nullptr || looperGetFromConsumerB() != prepared ||
        android::Looper::getForThread().get() != prepared) {
        gResult.store(1, std::memory_order_release);
    } else {
        gResult.store(0, std::memory_order_release);
    }
    return nullptr;
}

}  // namespace

__attribute__((constructor)) static void prepareLooperDuringDlopen() {
    pthread_t worker;
    const int createError = pthread_create(&worker, nullptr, prepareLooperOnWorker, nullptr);
    if (createError != 0) {
        gResult.store(100 + createError, std::memory_order_release);
        return;
    }

    const int joinError = pthread_join(worker, nullptr);
    if (joinError != 0) {
        gResult.store(200 + joinError, std::memory_order_release);
    }
}

extern "C" int looperDlopenFixtureResult() {
    return gResult.load(std::memory_order_acquire);
}
