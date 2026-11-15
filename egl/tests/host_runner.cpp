#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <thread>
#include <vector>

static std::atomic<int> constructors{0};
static std::atomic<int> destructors{0};
static std::atomic<int> reentry_ok{0};
static const char* wrapper_path;

extern "C" void egl_test_driver_constructed() { ++constructors; }
extern "C" void egl_test_driver_destructed() { ++destructors; }

extern "C" void egl_test_driver_reentry() {
    void* handle = dlopen(wrapper_path, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) return;
    auto display = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLNativeDisplayType)>(
            dlsym(handle, "eglGetDisplay"));
    auto query = reinterpret_cast<__eglMustCastToProperFunctionPointerType(EGLAPIENTRY*)(const char*)>(
            dlsym(handle, "eglGetProcAddress"));
    if (display != nullptr && query != nullptr && display(nullptr) == EGL_NO_DISPLAY &&
        query("glRawInt") == nullptr) ++reentry_ok;
    dlclose(handle);
}

static void* symbol(void* handle, const char* name) {
    dlerror();
    void* value = dlsym(handle, name);
    const char* error = dlerror();
    if (error != nullptr) {
        std::cerr << "dlsym(" << name << "): " << error << '\n';
        std::abort();
    }
    return value;
}

static void must(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

static void* loader_lookup(void* handle,
        __eglMustCastToProperFunctionPointerType(EGLAPIENTRY* get_proc)(const char*),
        const char* requested) {
    if (void* direct = dlsym(handle, requested); direct != nullptr) return direct;
    if (auto queried = get_proc(requested); queried != nullptr)
        return reinterpret_cast<void*>(queried);
    const size_t length = strlen(requested);
    if (length >= 3 && strcmp(requested + length - 3, "OES") == 0) return nullptr;
    char candidate[256];
    if (length + 4 > sizeof(candidate)) return nullptr;
    memcpy(candidate, requested, length);
    memcpy(candidate + length, "OES", 4);
    return dlsym(handle, candidate);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    wrapper_path = argv[1];
    const char* driver_path = argv[2];
    void* first = dlopen(wrapper_path, RTLD_NOW | RTLD_LOCAL);
    must(first != nullptr, "wrapper dlopen");
    must(constructors == 1 && reentry_ok == 1, "driver constructor and initialization reentry");
    void* second = dlopen(wrapper_path, RTLD_NOW | RTLD_LOCAL);
    must(second != nullptr && constructors == 1, "same wrapper constructor runs once");

    auto display = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLNativeDisplayType)>(
            symbol(first, "eglGetDisplay"));
    auto query = reinterpret_cast<__eglMustCastToProperFunctionPointerType(EGLAPIENTRY*)(const char*)>(
            symbol(first, "eglGetProcAddress"));
    must(display(nullptr) == reinterpret_cast<EGLDisplay>(uintptr_t(0x1110)),
         "dlsym getDisplay reaches direct exported target");

    void* driver = dlopen(driver_path, RTLD_NOW | RTLD_NOLOAD);
    must(driver != nullptr, "fixture is loaded by wrapper's explicit dlopen");
    auto real_query = reinterpret_cast<__eglMustCastToProperFunctionPointerType(EGLAPIENTRY*)(const char*)>(
            symbol(driver, "eglGetProcAddress"));
    auto driver_queried_display = real_query("eglGetDisplay");

    auto queried_display = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLNativeDisplayType)>(
            query("eglGetDisplay"));
    must(queried_display == reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLNativeDisplayType)>(
                 query("eglGetDisplay")), "queried display wrapper address is stable");
    must(queried_display != nullptr && queried_display != display,
         "queried getDisplay is a protected wrapper distinct from dlsym entry");
    must(reinterpret_cast<void*>(queried_display) != reinterpret_cast<void*>(driver_queried_display),
         "queried getDisplay does not leak the raw fixture function pointer");
    must(queried_display(nullptr) == reinterpret_cast<EGLDisplay>(uintptr_t(0x2220)),
         "queried getDisplay preserves distinct query target");
    must(query("glAliasGetDisplay") == reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queried_display),
         "queried alias to protected display maps to its distinct wrapper");
    must(query("eglGetProcAddress") == reinterpret_cast<__eglMustCastToProperFunctionPointerType>(query),
         "query function cannot bypass wrapper");
    must(query("glDlsymOnly") == nullptr, "driver query null remains null for dynamic-only symbol");
    must(query("notARealEglSymbol") == nullptr, "unknown query remains null");

    must(dlsym(first, "eglGetPlatformDisplay") == nullptr &&
         dlsym(first, "eglGetPlatformDisplayEXT") == nullptr,
         "platform entries are query-only, not dynamic exports");
    auto platform = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLAttrib*)>(
            query("eglGetPlatformDisplay"));
    auto platform_ext = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLint*)>(
            query("eglGetPlatformDisplayEXT"));
    EGLAttrib core_attrs[] = {EGL_NONE};
    EGLint ext_attrs[] = {EGL_NONE};
    must(platform != nullptr && platform(0x77, nullptr, core_attrs) ==
         reinterpret_cast<EGLDisplay>(uintptr_t(0x3077)), "core platform query uses EGLAttrib signature");
    must(platform_ext != nullptr && platform_ext(0x88, nullptr, ext_attrs) ==
         reinterpret_cast<EGLDisplay>(uintptr_t(0x4088)), "EXT platform query uses EGLint signature");
    must(query("glAliasGetPlatformDisplay") ==
                 reinterpret_cast<__eglMustCastToProperFunctionPointerType>(platform) &&
         query("glAliasGetPlatformDisplayEXT") ==
                 reinterpret_cast<__eglMustCastToProperFunctionPointerType>(platform_ext),
         "aliases to protected platform targets map to their typed wrappers");

    auto int_fn = reinterpret_cast<uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
            uint64_t,uint64_t,uint64_t,uint64_t,uint64_t)>(symbol(first, "glRawInt"));
    must(int_fn(1,2,3,4,5,6,7,8,9,10) == 951, "raw integer tail branch preserves stack arguments");
    auto float_fn = reinterpret_cast<double(*)(double,double,double,double,double,double,double,double,double,double)>(
            symbol(first, "glRawFloat"));
    must(std::abs(float_fn(1,2,3,4,5,6,7,8,9,10) - 385.0) < 1e-9,
         "raw floating tail branch preserves stack arguments");
    auto mixed_fn = reinterpret_cast<uint64_t(*)(uint64_t,double,uint64_t,double,uint64_t,double,
            uint64_t,double,uint64_t,double,uint64_t,double,uint64_t,double,uint64_t,double,
            uint64_t,double,uint64_t,double)>(symbol(first, "glRawMixed"));
    must(mixed_fn(1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10) == 6451,
         "raw mixed integer/double tail branch preserves register and stack arguments");
    must(symbol(first, "glTestOES") != nullptr, "OES fallback candidate dynamically exported");
    // Match Loader order: direct dlsym, then getProcAddress, then the OES spelling.
    must(loader_lookup(first, query, "glRawInt") == symbol(first, "glRawInt"),
         "Loader gives direct dlsym priority over a null getProc result");
    must(loader_lookup(first, query, "glTest") == symbol(first, "glTestOES"),
         "Loader falls through null dlsym/getProc to OES candidate");

    must(dlclose(driver) == 0, "close extra fixture probe reference before lifecycle assertions");

    std::atomic<bool> threads_ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) workers.emplace_back([&] {
        for (int i = 0; i < 2000; ++i) {
            auto concurrent_get_display = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLNativeDisplayType)>(
                    query("eglGetDisplay"));
            auto concurrent_platform = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLAttrib*)>(
                    query("eglGetPlatformDisplay"));
            auto concurrent_platform_ext = reinterpret_cast<EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLint*)>(
                    query("eglGetPlatformDisplayEXT"));
            EGLAttrib local_core_attrs[] = {EGL_NONE};
            EGLint local_ext_attrs[] = {EGL_NONE};
            if (query("notARealEglSymbol") != nullptr || concurrent_get_display == nullptr ||
                concurrent_get_display(nullptr) != reinterpret_cast<EGLDisplay>(uintptr_t(0x2220)) ||
                concurrent_platform == nullptr || concurrent_platform(0x77, nullptr, local_core_attrs) !=
                    reinterpret_cast<EGLDisplay>(uintptr_t(0x3077)) ||
                concurrent_platform_ext == nullptr || concurrent_platform_ext(0x88, nullptr, local_ext_attrs) !=
                    reinterpret_cast<EGLDisplay>(uintptr_t(0x4088)) || display(nullptr) !=
                reinterpret_cast<EGLDisplay>(uintptr_t(0x1110)) || int_fn(1,2,3,4,5,6,7,8,9,10) != 951)
                threads_ok = false;
        }
    });
    for (auto& worker : workers) worker.join();
    must(threads_ok, "concurrent queries and forwarding");

    dlclose(first);
    must(constructors == 1 && destructors == 0, "driver remains loaded while wrapper ref exists");
    dlclose(second);
    must(destructors == 1, "last wrapper close runs driver destructor");
    void* reopened = dlopen(wrapper_path, RTLD_NOW | RTLD_LOCAL);
    must(reopened != nullptr && constructors == 2 && reentry_ok == 2,
         "wrapper can reopen and driver constructor reruns");
    dlclose(reopened);
    must(destructors == 2, "reopened wrapper closes fixture");
    std::cout << "host EGL wrapper checks passed\n";
    return 0;
}
