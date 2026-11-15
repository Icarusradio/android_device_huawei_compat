#include <cstdlib>
#include <dlfcn.h>

extern "C" void egl_test_driver_constructed() {}
extern "C" void egl_test_driver_destructed() {}
extern "C" void egl_test_driver_reentry() {}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) return 3;
    dlclose(handle);
    return 0;
}
