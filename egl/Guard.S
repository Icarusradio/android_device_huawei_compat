/* Copyright (C) 2026 The LineageOS Project. SPDX-License-Identifier: Apache-2.0 */

#if defined(__aarch64__)
.text
.p2align 2
.hidden huawei_egl_guard_call
.global huawei_egl_guard_call
.type huawei_egl_guard_call, %function
huawei_egl_guard_call:
    .cfi_startproc
    stp x19, x30, [sp, #-16]!
    .cfi_def_cfa_offset 16
    .cfi_offset x19, -16
    .cfi_offset x30, -8
    mov x19, x18
    mov x16, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    blr x16
    mov x18, x19
    ldp x19, x30, [sp], #16
    .cfi_def_cfa_offset 0
    .cfi_restore x19
    .cfi_restore x30
    ret
    .cfi_endproc
.size huawei_egl_guard_call, .-huawei_egl_guard_call
#elif defined(__arm__)
.text
.arm
.p2align 2
.hidden huawei_egl_guard_call
.global huawei_egl_guard_call
.type huawei_egl_guard_call, %function
huawei_egl_guard_call:
    mov ip, r0
    mov r0, r1
    mov r1, r2
    mov r2, r3
    bx ip
.size huawei_egl_guard_call, .-huawei_egl_guard_call
#elif defined(__x86_64__)
.text
.p2align 4
.hidden huawei_egl_guard_call
.global huawei_egl_guard_call
.type huawei_egl_guard_call, @function
huawei_egl_guard_call:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    jmp *%rax
.size huawei_egl_guard_call, .-huawei_egl_guard_call
#else
#error Unsupported EGL wrapper architecture
#endif

.section .note.GNU-stack,"",%progbits
