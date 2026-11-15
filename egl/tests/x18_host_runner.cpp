/* AArch64-only mechanism check; test code intentionally has no SCS instrumentation. */
using uintptr_t = unsigned long;

extern "C" uintptr_t egl_test_probe_call(void* target, uintptr_t argument,
        uintptr_t* before, uintptr_t* after, uintptr_t shadow_stack);
extern "C" uintptr_t egl_test_clear_x18(uintptr_t);
extern "C" uintptr_t egl_test_guard_adapter(uintptr_t);

int main() {
    alignas(16) uintptr_t shadow_stack[32] = {};
    uintptr_t raw_before = 0;
    uintptr_t raw_after = 0;
    uintptr_t guarded_before = 0;
    uintptr_t guarded_after = 0;
    const uintptr_t shadow = reinterpret_cast<uintptr_t>(shadow_stack);

    const uintptr_t raw_result = egl_test_probe_call(
            reinterpret_cast<void*>(egl_test_clear_x18), 0,
            &raw_before, &raw_after, shadow);
    const uintptr_t guarded_result = egl_test_probe_call(
            reinterpret_cast<void*>(egl_test_guard_adapter), 0,
            &guarded_before, &guarded_after, shadow);

    if (raw_result != 0x32 || raw_before != shadow || raw_after != 0) return 1;
    if (guarded_result != 0x32 || guarded_before != shadow || guarded_after != shadow) return 2;
    return 0;
}
