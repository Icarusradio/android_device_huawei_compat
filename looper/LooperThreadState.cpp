/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>

#include <cstring>

#include <log/log.h>
#include <utils/Looper.h>

namespace android {
namespace {

// CameraDaemon explicitly loads this GLOBAL shim to avoid the loader-lock
// deadlock when a dlopen constructor waits for a worker registering a C++ TLS
// destructor. Own all three Looper TLS entry points; forwarding any one to
// libutils would split the thread state because those exports are PROTECTED.
// Looper objects, polling, and all other behavior still come from real libutils.
pthread_once_t gLooperTlsOnce = PTHREAD_ONCE_INIT;
pthread_key_t gLooperTlsKey;

void looperThreadDestructor(void* value) {
    auto* looper = static_cast<Looper*>(value);
    if (looper != nullptr) {
        looper->decStrong(&gLooperTlsKey);
    }
}

void initializeLooperTlsKey() {
    const int error = pthread_key_create(&gLooperTlsKey, looperThreadDestructor);
    LOG_ALWAYS_FATAL_IF(error != 0, "Could not allocate Looper TLS key: %s", strerror(error));
}

void ensureLooperTlsKey() {
    const int error = pthread_once(&gLooperTlsOnce, initializeLooperTlsKey);
    LOG_ALWAYS_FATAL_IF(error != 0, "pthread_once failed for Looper TLS: %s", strerror(error));
}

}  // namespace

__attribute__((visibility("protected")))
sp<Looper> Looper::getForThread() {
    ensureLooperTlsKey();
    auto* looper = static_cast<Looper*>(pthread_getspecific(gLooperTlsKey));
    return sp<Looper>::fromExisting(looper);
}

__attribute__((visibility("protected")))
void Looper::setForThread(const sp<Looper>& looper) {
    ensureLooperTlsKey();
    sp<Looper> old = getForThread();

    if (looper != nullptr) {
        looper->incStrong(&gLooperTlsKey);
    }

    const int error = pthread_setspecific(gLooperTlsKey, looper.get());
    LOG_ALWAYS_FATAL_IF(error != 0, "Could not set Looper TLS value: %s", strerror(error));

    if (old != nullptr) {
        old->decStrong(&gLooperTlsKey);
    }
}

__attribute__((visibility("protected")))
sp<Looper> Looper::prepare(int opts) {
    const bool allowNonCallbacks = opts & PREPARE_ALLOW_NON_CALLBACKS;
    sp<Looper> looper = Looper::getForThread();
    if (looper == nullptr) {
        looper = sp<Looper>::make(allowNonCallbacks);
        Looper::setForThread(looper);
    }
    if (looper->getAllowNonCallbacks() != allowNonCallbacks) {
        ALOGW("Looper already prepared for this thread with a different value for "
              "the LOOPER_PREPARE_ALLOW_NON_CALLBACKS option.");
    }
    return looper;
}

}  // namespace android
