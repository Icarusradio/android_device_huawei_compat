/* Host-only dlopen fixture for the Huawei EGL wrapper tests. */
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <stdint.h>
#include <string.h>

extern "C" void egl_test_driver_constructed();
extern "C" void egl_test_driver_destructed();
extern "C" void egl_test_driver_reentry();

__attribute__((constructor)) static void fixture_init() {
    egl_test_driver_constructed();
    egl_test_driver_reentry();
}

__attribute__((destructor)) static void fixture_fini() {
    egl_test_driver_destructed();
}

extern "C" __attribute__((visibility("default"))) EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType) {
    return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(0x1110));
}

static EGLDisplay EGLAPIENTRY queried_get_display(EGLNativeDisplayType) {
    return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(0x2220));
}

static EGLDisplay EGLAPIENTRY queried_platform_core(EGLenum platform, void*, const EGLAttrib*) {
    return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(0x3000) + platform);
}

static EGLDisplay EGLAPIENTRY queried_platform_ext(EGLenum platform, void*, const EGLint*) {
    return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(0x4000) + platform);
}

extern "C" __attribute__((visibility("default"))) __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(
        const char* name) {
    if (name == nullptr) return nullptr;
    if (strcmp(name, "eglGetDisplay") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_get_display);
    if (strcmp(name, "eglGetPlatformDisplay") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_platform_core);
    if (strcmp(name, "eglGetPlatformDisplayEXT") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_platform_ext);
    if (strcmp(name, "eglGetProcAddress") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(eglGetProcAddress);
    if (strcmp(name, "glAliasGetDisplay") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_get_display);
    if (strcmp(name, "glAliasGetPlatformDisplay") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_platform_core);
    if (strcmp(name, "glAliasGetPlatformDisplayEXT") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_platform_ext);
    if (strcmp(name, "glDlsymOnly") == 0) return nullptr;
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void glDlsymOnly() {}
extern "C" __attribute__((visibility("default"))) void glTestOES() {}

extern "C" __attribute__((visibility("default"))) uint64_t glRawInt(
        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
        uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9) {
    return a0 + 3*a1 + 5*a2 + 7*a3 + 11*a4 + 13*a5 + 17*a6 + 19*a7 + 23*a8 + 29*a9;
}

extern "C" __attribute__((visibility("default"))) double glRawFloat(
        double a0, double a1, double a2, double a3, double a4,
        double a5, double a6, double a7, double a8, double a9) {
    return a0 + 2*a1 + 3*a2 + 4*a3 + 5*a4 + 6*a5 + 7*a6 + 8*a7 + 9*a8 + 10*a9;
}

#ifndef EGL_TEST_OMIT_RAW_MIXED
extern "C" __attribute__((visibility("default"))) uint64_t glRawMixed(
        uint64_t i0, double d0, uint64_t i1, double d1, uint64_t i2, double d2,
        uint64_t i3, double d3, uint64_t i4, double d4, uint64_t i5, double d5,
        uint64_t i6, double d6, uint64_t i7, double d7, uint64_t i8, double d8,
        uint64_t i9, double d9) {
    double sum = d0+d1+d2+d3+d4+d5+d6+d7+d8+d9;
    return i0 + 3*i1 + 5*i2 + 7*i3 + 11*i4 + 13*i5 + 17*i6 + 19*i7 +
           23*i8 + 29*i9 + static_cast<uint64_t>(sum * 100);
}
#endif
