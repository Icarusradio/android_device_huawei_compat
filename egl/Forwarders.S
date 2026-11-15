/* Copyright (C) 2026 The LineageOS Project. SPDX-License-Identifier: Apache-2.0 */

#ifndef HUAWEI_EGL_SYMBOLS_FILE
#define HUAWEI_EGL_SYMBOLS_FILE "DriverSymbols.inc"
#endif

#if defined(__aarch64__)
.text
.p2align 2
.macro HUAWEI_FORWARD index, name
    .global \name
    .type \name, %function
\name:
    adrp x17, huawei_egl_driver_targets
    add x17, x17, :lo12:huawei_egl_driver_targets
    ldr x16, [x17, #(\index * 8)]
    cbz x16, huawei_egl_uninitialized
    br x16
    .size \name, .-\name
.endm
#define HUAWEI_EGL_CORE_SYMBOL(index, symbol)
#define HUAWEI_EGL_SYMBOL(index, symbol) HUAWEI_FORWARD index, symbol
#include HUAWEI_EGL_SYMBOLS_FILE
#elif defined(__arm__)
.text
.arm
.p2align 2
.macro HUAWEI_FORWARD index, name
    .global \name
    .type \name, %function
\name:
    ldr ip, .Ldelta_\name
.Lpc_\name:
    add ip, pc, ip
    ldr ip, [ip, #(\index * 4)]
    cmp ip, #0
    beq huawei_egl_uninitialized
    bx ip
    .align 2
.Ldelta_\name:
    .word huawei_egl_driver_targets - (.Lpc_\name + 8)
    .size \name, .-\name
.endm
#define HUAWEI_EGL_CORE_SYMBOL(index, symbol)
#define HUAWEI_EGL_SYMBOL(index, symbol) HUAWEI_FORWARD index, symbol
#include HUAWEI_EGL_SYMBOLS_FILE
#elif defined(__x86_64__)
.text
.p2align 4
.macro HUAWEI_FORWARD index, name
    .global \name
    .type \name, @function
\name:
    movq huawei_egl_driver_targets+(\index * 8)(%rip), %r11
    testq %r11, %r11
    je huawei_egl_uninitialized
    jmp *%r11
    .size \name, .-\name
.endm
#define HUAWEI_EGL_CORE_SYMBOL(index, symbol)
#define HUAWEI_EGL_SYMBOL(index, symbol) HUAWEI_FORWARD index, symbol
#include HUAWEI_EGL_SYMBOLS_FILE
#else
#error Unsupported EGL wrapper architecture
#endif

.section .note.GNU-stack,"",%progbits
