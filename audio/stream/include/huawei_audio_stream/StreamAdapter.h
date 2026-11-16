// Copyright (C) 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <hardware/audio.h>

namespace huawei::audio::stream {

using CloseOutput = void (*)(void* cookie, audio_stream_out* legacy);
using CloseInput = void (*)(void* cookie, audio_stream_in* legacy);

// On success, the adapter owns the legacy stream until the matching close call.
// On failure, it leaves legacy ownership with the caller and clears *wrapped.
int wrapOutput(audio_stream_out* legacy, CloseOutput close, void* cookie,
               audio_stream_out** wrapped) noexcept;
int wrapInput(audio_stream_in* legacy, CloseInput close, void* cookie,
              audio_stream_in** wrapped) noexcept;

// Call after external stream operations have stopped. The device close callback
// must finish any callback-thread cleanup it owns before it returns.
void closeOutput(audio_stream_out* wrapped) noexcept;
void closeInput(audio_stream_in* wrapped) noexcept;

}  // namespace huawei::audio::stream
