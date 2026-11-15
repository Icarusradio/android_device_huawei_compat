/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <EGL/egl.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HUAWEI_EGL_SYMBOLS_FILE
#define HUAWEI_EGL_SYMBOLS_FILE "DriverSymbols.inc"
#endif

#ifndef HUAWEI_EGL_DRIVER_PATH
#if defined(__LP64__)
#define HUAWEI_EGL_DRIVER_PATH "/vendor/lib64/egl/libGLES_mali.so"
#else
#define HUAWEI_EGL_DRIVER_PATH "/vendor/lib/egl/libGLES_mali.so"
#endif
#endif

extern "C" __attribute__((visibility("hidden"))) uintptr_t huawei_egl_guard_call(
        void* target, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2);
extern "C" __attribute__((visibility("hidden"), noreturn)) void huawei_egl_uninitialized();
extern "C" __attribute__((visibility("hidden"))) EGLDisplay huawei_egl_get_platform_display(
        EGLenum platform, void* display, const EGLAttrib* attributes);
extern "C" __attribute__((visibility("hidden"))) EGLDisplay EGLAPIENTRY
huawei_egl_queried_get_display(EGLNativeDisplayType display);
extern "C" __attribute__((visibility("hidden"))) EGLDisplay
huawei_egl_get_platform_display_ext(EGLenum platform, void* display, const EGLint* attributes);
extern "C" __attribute__((visibility("hidden"))) void* huawei_egl_driver_targets[];
namespace {

enum : uint32_t { kUninitialized = 0, kLoading = 1, kReady = 2 };

#define HUAWEI_EGL_SYMBOL(index, symbol) +1
#define HUAWEI_EGL_CORE_SYMBOL(index, symbol) +1
constexpr size_t kDriverSymbolCount = 0
#include HUAWEI_EGL_SYMBOLS_FILE
        ;
#undef HUAWEI_EGL_SYMBOL
#undef HUAWEI_EGL_CORE_SYMBOL

struct DriverSymbol {
    const char* name;
    size_t index;
};

#define HUAWEI_EGL_SYMBOL(index, symbol) {#symbol, index},
#define HUAWEI_EGL_CORE_SYMBOL(index, symbol) {#symbol, index},
constexpr DriverSymbol kDriverSymbols[] = {
#include HUAWEI_EGL_SYMBOLS_FILE
};
#undef HUAWEI_EGL_SYMBOL
#undef HUAWEI_EGL_CORE_SYMBOL

static_assert(sizeof(kDriverSymbols) / sizeof(kDriverSymbols[0]) == kDriverSymbolCount);

uint32_t gState;
void* gDriverHandle;
void* gRealGetDisplay;
void* gRealGetProcAddress;
void* gQueriedGetDisplay;
void* gQueriedGetPlatformDisplay;
void* gQueriedGetPlatformDisplayExt;

void logFailure(const char* operation, const char* detail) {
    char message[512];
    const int length = snprintf(message, sizeof(message),
                                "libGLES_huawei_compat: %s failed: %s\n", operation,
                                detail != nullptr ? detail : "unknown error");
    if (length > 0) {
        const size_t bytes = static_cast<size_t>(length) < sizeof(message)
                                     ? static_cast<size_t>(length)
                                     : sizeof(message) - 1;
        (void)write(STDERR_FILENO, message, bytes);
    }
}

[[noreturn]] void abortInitialization(const char* operation, const char* detail) {
    logFailure(operation, detail);
    abort();
}

template <typename Fn>
uintptr_t guarded(Fn function, uintptr_t arg0 = 0, uintptr_t arg1 = 0, uintptr_t arg2 = 0) {
    return huawei_egl_guard_call(reinterpret_cast<void*>(function), arg0, arg1, arg2);
}

uint32_t loadState() {
    return __atomic_load_n(&gState, __ATOMIC_ACQUIRE);
}

void cacheQueriedTarget(void*& slot, void* value, const char* name) {
    void* expected = nullptr;
    if (__atomic_compare_exchange_n(&slot, &expected, value, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE) || expected == value) {
        return;
    }
    abortInitialization("unstable getProcAddress target", name);
}

void* loadPointer(void*& slot) {
    return __atomic_load_n(&slot, __ATOMIC_ACQUIRE);
}

void* findQueriedAlias(void* target) {
    if (target == nullptr) return nullptr;
    if (target == gRealGetProcAddress) return reinterpret_cast<void*>(&eglGetProcAddress);
    if (target == loadPointer(gQueriedGetDisplay)) {
        return reinterpret_cast<void*>(&huawei_egl_queried_get_display);
    }
    if (target == gRealGetDisplay) {
        return reinterpret_cast<void*>(&eglGetDisplay);
    }
    if (target == loadPointer(gQueriedGetPlatformDisplay)) {
        return reinterpret_cast<void*>(&huawei_egl_get_platform_display);
    }
    if (target == loadPointer(gQueriedGetPlatformDisplayExt)) {
        return reinterpret_cast<void*>(&huawei_egl_get_platform_display_ext);
    }
    return nullptr;
}

}  // namespace

extern "C" {
__attribute__((visibility("hidden"))) void* huawei_egl_driver_targets[kDriverSymbolCount] = {};
}

extern "C" __attribute__((visibility("hidden"), noreturn)) void huawei_egl_uninitialized() {
    logFailure("forwarder", "driver target unavailable before initialization or during shutdown");
    abort();
}

extern "C" __attribute__((visibility("hidden"))) EGLDisplay huawei_egl_get_platform_display(
        EGLenum platform, void* display, const EGLAttrib* attributes) {
    if (loadState() != kReady) return EGL_NO_DISPLAY;
    auto* target = loadPointer(gQueriedGetPlatformDisplay);
    return target != nullptr
                   ? reinterpret_cast<EGLDisplay>(guarded(
                             target, platform, reinterpret_cast<uintptr_t>(display),
                             reinterpret_cast<uintptr_t>(attributes)))
                   : EGL_NO_DISPLAY;
}

extern "C" __attribute__((visibility("hidden"))) EGLDisplay huawei_egl_get_platform_display_ext(
        EGLenum platform, void* display, const EGLint* attributes) {
    if (loadState() != kReady) return EGL_NO_DISPLAY;
    auto* target = loadPointer(gQueriedGetPlatformDisplayExt);
    return target != nullptr
                   ? reinterpret_cast<EGLDisplay>(guarded(
                             target, platform, reinterpret_cast<uintptr_t>(display),
                             reinterpret_cast<uintptr_t>(attributes)))
                   : EGL_NO_DISPLAY;
}

extern "C" __attribute__((visibility("default"))) EGLDisplay EGLAPIENTRY eglGetDisplay(
        EGLNativeDisplayType display) {
    if (loadState() != kReady || gRealGetDisplay == nullptr) return EGL_NO_DISPLAY;
    return reinterpret_cast<EGLDisplay>(guarded(
            gRealGetDisplay, reinterpret_cast<uintptr_t>(display)));
}

extern "C" __attribute__((visibility("hidden"))) EGLDisplay EGLAPIENTRY
huawei_egl_queried_get_display(EGLNativeDisplayType display) {
    if (loadState() != kReady) return EGL_NO_DISPLAY;
    void* target = loadPointer(gQueriedGetDisplay);
    return target != nullptr
                   ? reinterpret_cast<EGLDisplay>(guarded(
                             target, reinterpret_cast<uintptr_t>(display)))
                   : EGL_NO_DISPLAY;
}

extern "C" __attribute__((visibility("default"))) __eglMustCastToProperFunctionPointerType
EGLAPIENTRY eglGetProcAddress(const char* name) {
    if (name == nullptr || loadState() != kReady || gRealGetProcAddress == nullptr) return nullptr;
    void* target = reinterpret_cast<void*>(guarded(gRealGetProcAddress,
                                                    reinterpret_cast<uintptr_t>(name)));
    if (target == nullptr) return nullptr;

    if (strcmp(name, "eglGetDisplay") == 0) {
        cacheQueriedTarget(gQueriedGetDisplay, target, name);
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
                &huawei_egl_queried_get_display);
    }
    if (strcmp(name, "eglGetPlatformDisplay") == 0) {
        cacheQueriedTarget(gQueriedGetPlatformDisplay, target, name);
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
                &huawei_egl_get_platform_display);
    }
    if (strcmp(name, "eglGetPlatformDisplayEXT") == 0) {
        cacheQueriedTarget(gQueriedGetPlatformDisplayExt, target, name);
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
                &huawei_egl_get_platform_display_ext);
    }
    if (void* wrapper = findQueriedAlias(target); wrapper != nullptr) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(wrapper);
    }
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(target);
}

extern "C" __attribute__((constructor, visibility("hidden"))) void huawei_egl_initialize() {
    uint32_t expected = kUninitialized;
    if (!__atomic_compare_exchange_n(&gState, &expected, kLoading, false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return;
    }

    gDriverHandle = reinterpret_cast<void*>(guarded(
            dlopen, reinterpret_cast<uintptr_t>(HUAWEI_EGL_DRIVER_PATH), RTLD_NOW | RTLD_LOCAL));
    if (gDriverHandle == nullptr) abortInitialization("dlopen", HUAWEI_EGL_DRIVER_PATH);

    for (const DriverSymbol& symbol : kDriverSymbols) {
        void* address = reinterpret_cast<void*>(guarded(
                dlsym, reinterpret_cast<uintptr_t>(gDriverHandle),
                reinterpret_cast<uintptr_t>(symbol.name)));
        if (address == nullptr) abortInitialization("dlsym", symbol.name);
        huawei_egl_driver_targets[symbol.index] = address;
        if (strcmp(symbol.name, "eglGetDisplay") == 0) gRealGetDisplay = address;
        if (strcmp(symbol.name, "eglGetProcAddress") == 0) gRealGetProcAddress = address;
    }

    if (gRealGetDisplay == nullptr || gRealGetProcAddress == nullptr) {
        abortInitialization("required EGL entry point", "eglGetDisplay/eglGetProcAddress");
    }
    __atomic_store_n(&gState, kReady, __ATOMIC_RELEASE);
}

extern "C" __attribute__((destructor, visibility("hidden"))) void huawei_egl_shutdown() {
    if (gDriverHandle == nullptr) return;
    __atomic_store_n(&gState, kLoading, __ATOMIC_RELEASE);
    void* handle = gDriverHandle;
    gDriverHandle = nullptr;
    for (size_t i = 0; i < kDriverSymbolCount; ++i) {
        huawei_egl_driver_targets[i] = nullptr;
    }
    if (guarded(dlclose, reinterpret_cast<uintptr_t>(handle)) != 0) {
        abortInitialization("dlclose", "Mali driver handle");
    }
    gRealGetDisplay = nullptr;
    gRealGetProcAddress = nullptr;
    gQueriedGetDisplay = nullptr;
    gQueriedGetPlatformDisplay = nullptr;
    gQueriedGetPlatformDisplayExt = nullptr;
}
