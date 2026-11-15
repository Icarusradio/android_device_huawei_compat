/* AArch64-only test helpers. Do not compile this file into the product. */
#if defined(__aarch64__)
.text
.p2align 2
.global egl_test_probe_call
.type egl_test_probe_call, %function
egl_test_probe_call:
    .cfi_startproc
    stp x19, x20, [sp, #-48]!
    .cfi_def_cfa_offset 48
    .cfi_offset x19, -48
    .cfi_offset x20, -40
    stp x21, x22, [sp, #16]
    .cfi_offset x21, -32
    .cfi_offset x22, -24
    stp x23, x30, [sp, #32]
    .cfi_offset x23, -16
    .cfi_offset x30, -8
    mov x19, x0                  // target
    mov x20, x1                  // target argument
    mov x21, x2                  // before sample
    mov x23, x3                  // after sample
    mov x22, x18                 // preserve harness x18 immediately
    mov x18, x4                  // install a writable test shadow-stack address
    str x18, [x21]
    mov x0, x20
    blr x19
    mov x20, x0
    str x18, [x23]                // sample before restoring harness x18
    mov x18, x22
    mov x0, x20
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #48
    .cfi_def_cfa_offset 0
    .cfi_restore x19
    .cfi_restore x20
    .cfi_restore x21
    .cfi_restore x22
    .cfi_restore x23
    .cfi_restore x30
    ret
    .cfi_endproc
.size egl_test_probe_call, .-egl_test_probe_call

/* Call the same corrupting target through the production guard entry. */
.p2align 2
.global egl_test_guard_adapter
.type egl_test_guard_adapter, %function
egl_test_guard_adapter:
    .cfi_startproc
    adr x0, egl_test_clear_x18
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    b huawei_egl_guard_call
    .cfi_endproc
.size egl_test_guard_adapter, .-egl_test_guard_adapter

/* Deliberately corrupt x18, then return a recognizable value. */
.macro CLEAR_X18 name, result
.p2align 2
.global \name
.type \name, %function
\name:
    mov x18, xzr
    mov x0, #\result
    ret
.size \name, .-\name
.endm

CLEAR_X18 egl_test_clear_x18, 0x32
#endif

.section .note.GNU-stack,"",%progbits
