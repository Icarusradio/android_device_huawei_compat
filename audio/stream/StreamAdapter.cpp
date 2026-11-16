// Copyright (C) 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include "huawei_audio_stream/StreamAdapter.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace huawei::audio::stream {
namespace {

constexpr size_t kCommonBytes = 112;
constexpr size_t kOutputBytes = 200;
constexpr size_t kInputBytes = 184;

static_assert(sizeof(void*) == 8, "This profile is for the verified 64-bit blob");
static_assert(sizeof(audio_stream) == kCommonBytes);
static_assert(sizeof(audio_microphone_characteristic_t) == 2296);
static_assert(offsetof(audio_microphone_characteristic_t, channel_mapping) == 72);

#define ASSERT_COMMON_FIELD(field, offset) \
    static_assert(offsetof(audio_stream, field) == offset, "legacy common ABI: " #field)
ASSERT_COMMON_FIELD(get_sample_rate, 0);
ASSERT_COMMON_FIELD(set_sample_rate, 8);
ASSERT_COMMON_FIELD(get_buffer_size, 16);
ASSERT_COMMON_FIELD(get_channels, 24);
ASSERT_COMMON_FIELD(get_format, 32);
ASSERT_COMMON_FIELD(set_format, 40);
ASSERT_COMMON_FIELD(standby, 48);
ASSERT_COMMON_FIELD(dump, 56);
ASSERT_COMMON_FIELD(get_device, 64);
ASSERT_COMMON_FIELD(set_device, 72);
ASSERT_COMMON_FIELD(set_parameters, 80);
ASSERT_COMMON_FIELD(get_parameters, 88);
ASSERT_COMMON_FIELD(add_audio_effect, 96);
ASSERT_COMMON_FIELD(remove_audio_effect, 104);
#undef ASSERT_COMMON_FIELD

#define ASSERT_OUT_FIELD(field, offset) \
    static_assert(offsetof(audio_stream_out, field) == offset, "legacy output ABI: " #field)
static_assert(offsetof(audio_stream_out, common) == 0);
ASSERT_OUT_FIELD(get_latency, 112);
ASSERT_OUT_FIELD(set_volume, 120);
ASSERT_OUT_FIELD(write, 128);
ASSERT_OUT_FIELD(get_render_position, 136);
ASSERT_OUT_FIELD(get_next_write_timestamp, 144);
ASSERT_OUT_FIELD(set_callback, 152);
ASSERT_OUT_FIELD(pause, 160);
ASSERT_OUT_FIELD(resume, 168);
ASSERT_OUT_FIELD(drain, 176);
ASSERT_OUT_FIELD(flush, 184);
ASSERT_OUT_FIELD(get_presentation_position, 192);
static_assert(offsetof(audio_stream_out, get_presentation_position) +
              sizeof(audio_stream_out::get_presentation_position) == kOutputBytes);
#undef ASSERT_OUT_FIELD

#define ASSERT_IN_FIELD(field, offset) \
    static_assert(offsetof(audio_stream_in, field) == offset, "legacy input ABI: " #field)
static_assert(offsetof(audio_stream_in, common) == 0);
ASSERT_IN_FIELD(set_gain, 112);
ASSERT_IN_FIELD(read, 120);
ASSERT_IN_FIELD(get_input_frames_lost, 128);
ASSERT_IN_FIELD(get_capture_position, 136);
ASSERT_IN_FIELD(start, 144);
ASSERT_IN_FIELD(stop, 152);
ASSERT_IN_FIELD(create_mmap_buffer, 160);
ASSERT_IN_FIELD(get_mmap_position, 168);
ASSERT_IN_FIELD(get_active_microphones, 176);
static_assert(offsetof(audio_stream_in, get_active_microphones) +
              sizeof(audio_stream_in::get_active_microphones) == kInputBytes);
#undef ASSERT_IN_FIELD

static_assert(offsetof(audio_stream_out, get_audio_description_mix_level) == 0x110);
static_assert(offsetof(audio_stream_in, set_microphone_field_dimension) == 0xc0);

struct OutputAdapter {
    audio_stream_out stream{};
    audio_stream_out legacy_callbacks{};
    audio_stream_out* legacy = nullptr;
    CloseOutput close = nullptr;
    void* cookie = nullptr;
};

struct InputAdapter {
    audio_stream_in stream{};
    audio_stream_in legacy_callbacks{};
    audio_stream_in* legacy = nullptr;
    CloseInput close = nullptr;
    void* cookie = nullptr;
};

static_assert(std::is_standard_layout_v<OutputAdapter>);
static_assert(std::is_standard_layout_v<InputAdapter>);
static_assert(offsetof(OutputAdapter, stream) == 0);
static_assert(offsetof(InputAdapter, stream) == 0);
static_assert(std::is_trivially_copyable_v<audio_stream_out>);
static_assert(std::is_trivially_copyable_v<audio_stream_in>);

OutputAdapter* outputAdapter(audio_stream* stream) {
    return reinterpret_cast<OutputAdapter*>(stream);
}
const OutputAdapter* outputAdapter(const audio_stream* stream) {
    return reinterpret_cast<const OutputAdapter*>(stream);
}
OutputAdapter* outputAdapter(audio_stream_out* stream) {
    return reinterpret_cast<OutputAdapter*>(stream);
}
const OutputAdapter* outputAdapter(const audio_stream_out* stream) {
    return reinterpret_cast<const OutputAdapter*>(stream);
}
InputAdapter* inputAdapter(audio_stream* stream) {
    return reinterpret_cast<InputAdapter*>(stream);
}
const InputAdapter* inputAdapter(const audio_stream* stream) {
    return reinterpret_cast<const InputAdapter*>(stream);
}
InputAdapter* inputAdapter(audio_stream_in* stream) {
    return reinterpret_cast<InputAdapter*>(stream);
}
const InputAdapter* inputAdapter(const audio_stream_in* stream) {
    return reinterpret_cast<const InputAdapter*>(stream);
}

#define DEFINE_OUT_COMMON(name, result, signature, arguments) \
    result out_##name signature { \
        const OutputAdapter* adapter = outputAdapter(stream); \
        return adapter->legacy_callbacks.common.name arguments; \
    }

DEFINE_OUT_COMMON(get_sample_rate, uint32_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_OUT_COMMON(set_sample_rate, int, (audio_stream* stream, uint32_t rate),
                  (&adapter->legacy->common, rate))
DEFINE_OUT_COMMON(get_buffer_size, size_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_OUT_COMMON(get_channels, audio_channel_mask_t, (const audio_stream* stream),
                  (&adapter->legacy->common))
DEFINE_OUT_COMMON(get_format, audio_format_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_OUT_COMMON(set_format, int, (audio_stream* stream, audio_format_t format),
                  (&adapter->legacy->common, format))
DEFINE_OUT_COMMON(standby, int, (audio_stream* stream), (&adapter->legacy->common))
DEFINE_OUT_COMMON(dump, int, (const audio_stream* stream, int fd), (&adapter->legacy->common, fd))
DEFINE_OUT_COMMON(get_device, audio_devices_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_OUT_COMMON(set_device, int, (audio_stream* stream, audio_devices_t device),
                  (&adapter->legacy->common, device))
DEFINE_OUT_COMMON(set_parameters, int, (audio_stream* stream, const char* kv_pairs),
                  (&adapter->legacy->common, kv_pairs))
DEFINE_OUT_COMMON(get_parameters, char*, (const audio_stream* stream, const char* keys),
                  (&adapter->legacy->common, keys))
DEFINE_OUT_COMMON(add_audio_effect, int, (const audio_stream* stream, effect_handle_t effect),
                  (&adapter->legacy->common, effect))
DEFINE_OUT_COMMON(remove_audio_effect, int, (const audio_stream* stream, effect_handle_t effect),
                  (&adapter->legacy->common, effect))
#undef DEFINE_OUT_COMMON

#define DEFINE_IN_COMMON(name, result, signature, arguments) \
    result in_##name signature { \
        const InputAdapter* adapter = inputAdapter(stream); \
        return adapter->legacy_callbacks.common.name arguments; \
    }

DEFINE_IN_COMMON(get_sample_rate, uint32_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_IN_COMMON(set_sample_rate, int, (audio_stream* stream, uint32_t rate),
                 (&adapter->legacy->common, rate))
DEFINE_IN_COMMON(get_buffer_size, size_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_IN_COMMON(get_channels, audio_channel_mask_t, (const audio_stream* stream),
                 (&adapter->legacy->common))
DEFINE_IN_COMMON(get_format, audio_format_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_IN_COMMON(set_format, int, (audio_stream* stream, audio_format_t format),
                 (&adapter->legacy->common, format))
DEFINE_IN_COMMON(standby, int, (audio_stream* stream), (&adapter->legacy->common))
DEFINE_IN_COMMON(dump, int, (const audio_stream* stream, int fd), (&adapter->legacy->common, fd))
DEFINE_IN_COMMON(get_device, audio_devices_t, (const audio_stream* stream), (&adapter->legacy->common))
DEFINE_IN_COMMON(set_device, int, (audio_stream* stream, audio_devices_t device),
                 (&adapter->legacy->common, device))
DEFINE_IN_COMMON(set_parameters, int, (audio_stream* stream, const char* kv_pairs),
                 (&adapter->legacy->common, kv_pairs))
DEFINE_IN_COMMON(get_parameters, char*, (const audio_stream* stream, const char* keys),
                 (&adapter->legacy->common, keys))
DEFINE_IN_COMMON(add_audio_effect, int, (const audio_stream* stream, effect_handle_t effect),
                 (&adapter->legacy->common, effect))
DEFINE_IN_COMMON(remove_audio_effect, int, (const audio_stream* stream, effect_handle_t effect),
                 (&adapter->legacy->common, effect))
#undef DEFINE_IN_COMMON

#define DEFINE_OUT(name, result, signature, arguments) \
    result out_##name signature { \
        const OutputAdapter* adapter = outputAdapter(stream); \
        return adapter->legacy_callbacks.name arguments; \
    }

DEFINE_OUT(get_latency, uint32_t, (const audio_stream_out* stream), (adapter->legacy))
DEFINE_OUT(set_volume, int, (audio_stream_out* stream, float left, float right),
           (adapter->legacy, left, right))
DEFINE_OUT(write, ssize_t, (audio_stream_out* stream, const void* buffer, size_t bytes),
           (adapter->legacy, buffer, bytes))
DEFINE_OUT(get_render_position, int, (const audio_stream_out* stream, uint32_t* frames),
           (adapter->legacy, frames))
DEFINE_OUT(get_next_write_timestamp, int, (const audio_stream_out* stream, int64_t* timestamp),
           (adapter->legacy, timestamp))
DEFINE_OUT(set_callback, int,
           (audio_stream_out* stream, stream_callback_t callback, void* cookie),
           (adapter->legacy, callback, cookie))
DEFINE_OUT(pause, int, (audio_stream_out* stream), (adapter->legacy))
DEFINE_OUT(resume, int, (audio_stream_out* stream), (adapter->legacy))
DEFINE_OUT(drain, int, (audio_stream_out* stream, audio_drain_type_t type),
           (adapter->legacy, type))
DEFINE_OUT(flush, int, (audio_stream_out* stream), (adapter->legacy))
DEFINE_OUT(get_presentation_position, int,
           (const audio_stream_out* stream, uint64_t* frames, timespec* timestamp),
           (adapter->legacy, frames, timestamp))
#undef DEFINE_OUT

#define DEFINE_IN(name, result, signature, arguments) \
    result in_##name signature { \
        const InputAdapter* adapter = inputAdapter(stream); \
        return adapter->legacy_callbacks.name arguments; \
    }

DEFINE_IN(set_gain, int, (audio_stream_in* stream, float gain), (adapter->legacy, gain))
DEFINE_IN(read, ssize_t, (audio_stream_in* stream, void* buffer, size_t bytes),
          (adapter->legacy, buffer, bytes))
DEFINE_IN(get_input_frames_lost, uint32_t, (audio_stream_in* stream), (adapter->legacy))
DEFINE_IN(get_capture_position, int,
          (const audio_stream_in* stream, int64_t* frames, int64_t* time),
          (adapter->legacy, frames, time))
DEFINE_IN(start, int, (const audio_stream_in* stream), (adapter->legacy))
DEFINE_IN(stop, int, (const audio_stream_in* stream), (adapter->legacy))
DEFINE_IN(create_mmap_buffer, int,
          (const audio_stream_in* stream, int32_t min_size_frames, audio_mmap_buffer_info* info),
          (adapter->legacy, min_size_frames, info))
DEFINE_IN(get_mmap_position, int,
          (const audio_stream_in* stream, audio_mmap_position* position),
          (adapter->legacy, position))
DEFINE_IN(get_active_microphones, int,
          (const audio_stream_in* stream, audio_microphone_characteristic_t* microphones,
           size_t* count),
          (adapter->legacy, microphones, count))
#undef DEFINE_IN

void installOutputOptional(audio_stream_out* modern, const audio_stream_out& legacy) {
#define INSTALL(field) \
    if (legacy.field != nullptr) modern->field = out_##field
    INSTALL(set_volume);
    INSTALL(get_next_write_timestamp);
    INSTALL(set_callback);
    INSTALL(pause);
    INSTALL(resume);
    INSTALL(drain);
    INSTALL(flush);
    INSTALL(get_presentation_position);
#undef INSTALL
    // Fields from start onward are outside this blob profile and remain NULL.
}

void installInputOptional(audio_stream_in* modern, const audio_stream_in& legacy) {
#define INSTALL(field) \
    if (legacy.field != nullptr) modern->field = in_##field
    INSTALL(get_capture_position);
    INSTALL(start);
    INSTALL(stop);
    INSTALL(create_mmap_buffer);
    INSTALL(get_mmap_position);
    INSTALL(get_active_microphones);
#undef INSTALL
    // Microphone direction, metadata, and later fields are outside the profile.
}

bool hasRequiredOutput(const audio_stream_out& legacy) {
    const audio_stream& common = legacy.common;
    return common.get_sample_rate != nullptr && common.get_buffer_size != nullptr &&
           common.get_channels != nullptr && common.get_format != nullptr &&
           common.standby != nullptr && common.dump != nullptr &&
           common.set_parameters != nullptr && common.get_parameters != nullptr &&
           common.add_audio_effect != nullptr && common.remove_audio_effect != nullptr &&
           legacy.get_latency != nullptr && legacy.write != nullptr &&
           legacy.get_render_position != nullptr;
}

bool hasRequiredInput(const audio_stream_in& legacy) {
    const audio_stream& common = legacy.common;
    return common.get_sample_rate != nullptr && common.get_buffer_size != nullptr &&
           common.get_channels != nullptr && common.get_format != nullptr &&
           common.standby != nullptr && common.dump != nullptr &&
           common.set_parameters != nullptr && common.get_parameters != nullptr &&
           common.add_audio_effect != nullptr && common.remove_audio_effect != nullptr &&
           legacy.set_gain != nullptr && legacy.read != nullptr &&
           legacy.get_input_frames_lost != nullptr;
}

}  // namespace

int wrapOutput(audio_stream_out* legacy, CloseOutput close, void* cookie,
               audio_stream_out** wrapped) noexcept {
    if (wrapped == nullptr) return -EINVAL;
    *wrapped = nullptr;
    if (legacy == nullptr || close == nullptr) {
        return -EINVAL;
    }

    OutputAdapter* adapter = new (std::nothrow) OutputAdapter{};
    if (adapter == nullptr) return -ENOMEM;

    std::memcpy(&adapter->legacy_callbacks, legacy, kOutputBytes);
    if (!hasRequiredOutput(adapter->legacy_callbacks)) {
        delete adapter;
        return -EINVAL;
    }

    adapter->stream.common.get_sample_rate = out_get_sample_rate;
    adapter->stream.common.set_sample_rate = adapter->legacy_callbacks.common.set_sample_rate
                                                     ? out_set_sample_rate
                                                     : nullptr;
    adapter->stream.common.get_buffer_size = out_get_buffer_size;
    adapter->stream.common.get_channels = out_get_channels;
    adapter->stream.common.get_format = out_get_format;
    adapter->stream.common.set_format = adapter->legacy_callbacks.common.set_format
                                               ? out_set_format
                                               : nullptr;
    adapter->stream.common.standby = out_standby;
    adapter->stream.common.dump = out_dump;
    adapter->stream.common.get_device = adapter->legacy_callbacks.common.get_device
                                               ? out_get_device
                                               : nullptr;
    adapter->stream.common.set_device = adapter->legacy_callbacks.common.set_device
                                               ? out_set_device
                                               : nullptr;
    adapter->stream.common.set_parameters = out_set_parameters;
    adapter->stream.common.get_parameters = out_get_parameters;
    adapter->stream.common.add_audio_effect = out_add_audio_effect;
    adapter->stream.common.remove_audio_effect = out_remove_audio_effect;

    adapter->stream.get_latency = out_get_latency;
    adapter->stream.write = out_write;
    adapter->stream.get_render_position = out_get_render_position;
    installOutputOptional(&adapter->stream, adapter->legacy_callbacks);

    adapter->legacy = legacy;
    adapter->close = close;
    adapter->cookie = cookie;
    *wrapped = &adapter->stream;
    return 0;
}

int wrapInput(audio_stream_in* legacy, CloseInput close, void* cookie,
              audio_stream_in** wrapped) noexcept {
    if (wrapped == nullptr) return -EINVAL;
    *wrapped = nullptr;
    if (legacy == nullptr || close == nullptr) {
        return -EINVAL;
    }

    InputAdapter* adapter = new (std::nothrow) InputAdapter{};
    if (adapter == nullptr) return -ENOMEM;

    std::memcpy(&adapter->legacy_callbacks, legacy, kInputBytes);
    if (!hasRequiredInput(adapter->legacy_callbacks)) {
        delete adapter;
        return -EINVAL;
    }

    adapter->stream.common.get_sample_rate = in_get_sample_rate;
    adapter->stream.common.set_sample_rate = adapter->legacy_callbacks.common.set_sample_rate
                                                     ? in_set_sample_rate
                                                     : nullptr;
    adapter->stream.common.get_buffer_size = in_get_buffer_size;
    adapter->stream.common.get_channels = in_get_channels;
    adapter->stream.common.get_format = in_get_format;
    adapter->stream.common.set_format = adapter->legacy_callbacks.common.set_format
                                               ? in_set_format
                                               : nullptr;
    adapter->stream.common.standby = in_standby;
    adapter->stream.common.dump = in_dump;
    adapter->stream.common.get_device = adapter->legacy_callbacks.common.get_device
                                               ? in_get_device
                                               : nullptr;
    adapter->stream.common.set_device = adapter->legacy_callbacks.common.set_device
                                               ? in_set_device
                                               : nullptr;
    adapter->stream.common.set_parameters = in_set_parameters;
    adapter->stream.common.get_parameters = in_get_parameters;
    adapter->stream.common.add_audio_effect = in_add_audio_effect;
    adapter->stream.common.remove_audio_effect = in_remove_audio_effect;

    adapter->stream.set_gain = in_set_gain;
    adapter->stream.read = in_read;
    adapter->stream.get_input_frames_lost = in_get_input_frames_lost;
    adapter->stream.get_capture_position = nullptr;
    adapter->stream.start = nullptr;
    adapter->stream.stop = nullptr;
    adapter->stream.create_mmap_buffer = nullptr;
    adapter->stream.get_mmap_position = nullptr;
    adapter->stream.get_active_microphones = nullptr;
    installInputOptional(&adapter->stream, adapter->legacy_callbacks);

    adapter->legacy = legacy;
    adapter->close = close;
    adapter->cookie = cookie;
    *wrapped = &adapter->stream;
    return 0;
}

void closeOutput(audio_stream_out* wrapped) noexcept {
    if (wrapped == nullptr) return;
    OutputAdapter* adapter = outputAdapter(wrapped);
    audio_stream_out* legacy = adapter->legacy;
    CloseOutput close = adapter->close;
    void* cookie = adapter->cookie;
    close(cookie, legacy);
    delete adapter;
}

void closeInput(audio_stream_in* wrapped) noexcept {
    if (wrapped == nullptr) return;
    InputAdapter* adapter = inputAdapter(wrapped);
    audio_stream_in* legacy = adapter->legacy;
    CloseInput close = adapter->close;
    void* cookie = adapter->cookie;
    close(cookie, legacy);
    delete adapter;
}

}  // namespace huawei::audio::stream
