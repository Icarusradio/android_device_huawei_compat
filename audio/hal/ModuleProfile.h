/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/hardware.h>

namespace huawei::audio {

// Verify the loaded module's regular-file contents against a known profile.
// This identifies an ABI profile; it is not a cryptographic signature check.
int verifyModuleSha256(const hw_module_t* module, const char* expectedHex);

}  // namespace huawei::audio
