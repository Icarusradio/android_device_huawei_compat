// Copyright (C) 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <android/log.h>
#include <cutils/list.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>

#include "audio_hisi.h"
#include "huawei_audio_stream/StreamAdapter.h"

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #condition);                                                \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

extern "C" struct audio_module HMI;

namespace huawei::audio {
int verifyModuleSha256(const hw_module_t *module, const char *expectedHex);
}

namespace {

constexpr char kVerifiedProfileSha256[] =
    "5c882f570fee9778167f3d5a2e2f704892ec426f15b50f345c546cbf42005724";

struct Fixture {
  hisi_audio_hw_device device{};
  hw_module_t module{};
  hw_module_methods_t methods{};
  audio_stream_out output[2]{};
  audio_stream_in input{};
  audio_config expected_output_config{};
  audio_config expected_input_config{};
  int profile_result = 0;
  int module_open_result = 0;
  int output_result = 0;
  int input_result = 0;
  bool output_returns_null = false;
  bool input_returns_null = false;
  bool break_output_required = false;
  bool break_input_required = false;
  bool return_output_raw_on_error = false;
  bool return_input_raw_on_error = false;
  size_t output_index = 0;
  int profile_checks = 0;
  int module_opens = 0;
  int output_opens = 0;
  int input_opens = 0;
  int output_closes = 0;
  int input_closes = 0;
  int device_closes = 0;
  uint32_t output_rate = 48000;
  audio_stream_out *last_closed_output = nullptr;
  audio_stream_in *last_closed_input = nullptr;
  audio_io_handle_t expected_handle = 0;
  audio_devices_t expected_devices = AUDIO_DEVICE_NONE;
  audio_output_flags_t expected_output_flags = AUDIO_OUTPUT_FLAG_NONE;
  audio_input_flags_t expected_input_flags = AUDIO_INPUT_FLAG_NONE;
  const char *expected_address = nullptr;
  audio_source_t expected_source = AUDIO_SOURCE_DEFAULT;
};

Fixture g;
constexpr uint32_t kRateA = 48000;
constexpr uint32_t kRateB = 96000;

uint32_t getRateA(const audio_stream *) { return kRateA; }
uint32_t getRateB(const audio_stream *) { return kRateB; }
int setRate(audio_stream *, uint32_t) { return 0; }
size_t getBuffer(const audio_stream *) { return 1920; }
audio_channel_mask_t getChannels(const audio_stream *) {
  return AUDIO_CHANNEL_OUT_STEREO;
}
audio_format_t getFormat(const audio_stream *) {
  return AUDIO_FORMAT_PCM_16_BIT;
}
int setFormat(audio_stream *, audio_format_t) { return 0; }
int standby(audio_stream *) { return 0; }
int dump(const audio_stream *, int) { return 0; }
audio_devices_t getDevice(const audio_stream *) {
  return AUDIO_DEVICE_OUT_SPEAKER;
}
int setDevice(audio_stream *, audio_devices_t) { return 0; }
int setParameters(audio_stream *, const char *) { return 0; }
char *getParameters(const audio_stream *, const char *) { return nullptr; }
int addEffect(const audio_stream *, effect_handle_t) { return 0; }
int removeEffect(const audio_stream *, effect_handle_t) { return 0; }

uint32_t getLatency(const audio_stream_out *) { return 23; }
int setVolume(audio_stream_out *, float, float) { return 0; }
ssize_t writeData(audio_stream_out *stream, const void *, size_t size) {
  const auto *raw = reinterpret_cast<const audio_stream_out *>(
      reinterpret_cast<const char *>(stream) -
      offsetof(audio_stream_out, common));
  CHECK(raw == &g.output[0] || raw == &g.output[1]);
  return static_cast<ssize_t>(size);
}
int getRenderPosition(const audio_stream_out *, uint32_t *frames) {
  *frames = 17;
  return 0;
}
int getNextTimestamp(const audio_stream_out *, int64_t *timestamp) {
  *timestamp = 19;
  return 0;
}
int setCallback(audio_stream_out *, stream_callback_t, void *) { return 0; }
int pauseOutput(audio_stream_out *) { return 0; }
int resumeOutput(audio_stream_out *) { return 0; }
int drainOutput(audio_stream_out *, audio_drain_type_t) { return 0; }
int flushOutput(audio_stream_out *) { return 0; }
int presentationPosition(const audio_stream_out *, uint64_t *, timespec *) {
  return 0;
}

int setGain(audio_stream_in *, float) { return 0; }
ssize_t readData(audio_stream_in *stream, void *, size_t size) {
  const auto *raw = reinterpret_cast<const audio_stream_in *>(
      reinterpret_cast<const char *>(stream) -
      offsetof(audio_stream_in, common));
  CHECK(raw == &g.input);
  return static_cast<ssize_t>(size);
}
uint32_t inputFramesLost(audio_stream_in *) { return 0; }
int capturePosition(const audio_stream_in *, int64_t *, int64_t *) { return 0; }
int startInput(const audio_stream_in *) { return 0; }
int stopInput(const audio_stream_in *) { return 0; }
int createMmapBuffer(const audio_stream_in *, int32_t,
                     audio_mmap_buffer_info *) {
  return -ENOSYS;
}
int getMmapPosition(const audio_stream_in *, audio_mmap_position *) {
  return -ENOSYS;
}
int activeMicrophones(const audio_stream_in *,
                      audio_microphone_characteristic_t *, size_t *) {
  return -ENOSYS;
}

void fillCommon(audio_stream *stream) {
  stream->get_sample_rate = getRateA;
  stream->set_sample_rate = setRate;
  stream->get_buffer_size = getBuffer;
  stream->get_channels = getChannels;
  stream->get_format = getFormat;
  stream->set_format = setFormat;
  stream->standby = standby;
  stream->dump = dump;
  stream->get_device = getDevice;
  stream->set_device = setDevice;
  stream->set_parameters = setParameters;
  stream->get_parameters = getParameters;
  stream->add_audio_effect = addEffect;
  stream->remove_audio_effect = removeEffect;
}

void fillOutput(audio_stream_out *stream) {
  fillCommon(&stream->common);
  if (stream == &g.output[0] && g.output_rate == kRateB) {
    stream->common.get_sample_rate = getRateB;
  }
  stream->get_latency = getLatency;
  stream->set_volume = setVolume;
  stream->write = writeData;
  stream->get_render_position = getRenderPosition;
  stream->get_next_write_timestamp = getNextTimestamp;
  stream->set_callback = setCallback;
  stream->pause = pauseOutput;
  stream->resume = resumeOutput;
  stream->drain = drainOutput;
  stream->flush = flushOutput;
  stream->get_presentation_position = presentationPosition;
}

void fillInput(audio_stream_in *stream) {
  fillCommon(&stream->common);
  stream->set_gain = setGain;
  stream->read = readData;
  stream->get_input_frames_lost = inputFramesLost;
  stream->get_capture_position = capturePosition;
  stream->start = startInput;
  stream->stop = stopInput;
  stream->create_mmap_buffer = createMmapBuffer;
  stream->get_mmap_position = getMmapPosition;
  stream->get_active_microphones = activeMicrophones;
}

int fakeDeviceClose(hw_device_t *) {
  ++g.device_closes;
  return 0;
}

int fakeModuleOpen(const hw_module_t *, const char *, hw_device_t **out) {
  ++g.module_opens;
  if (g.module_open_result != 0)
    return g.module_open_result;
  *out = &g.device.common;
  return 0;
}

int fakeOutputOpen(hisi_audio_hw_device_t *device, audio_io_handle_t handle,
                   audio_devices_t devices, audio_output_flags_t flags,
                   audio_config *config, audio_stream_out **stream,
                   const char *address) {
  ++g.output_opens;
  CHECK(device == &g.device);
  CHECK(handle == g.expected_handle);
  CHECK(devices == g.expected_devices);
  CHECK(flags == g.expected_output_flags);
  CHECK(config == &g.expected_output_config);
  CHECK(address == g.expected_address);
  config->sample_rate = 44100;
  config->channel_mask = AUDIO_CHANNEL_OUT_MONO;
  config->format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
  if (g.output_result != 0) {
    if (g.return_output_raw_on_error)
      *stream = &g.output[g.output_index];
    return g.output_result;
  }
  if (g.output_returns_null) {
    *stream = nullptr;
    return 0;
  }
  fillOutput(&g.output[g.output_index]);
  if (g.break_output_required)
    g.output[g.output_index].write = nullptr;
  *stream = &g.output[g.output_index];
  return 0;
}

void fakeOutputClose(hisi_audio_hw_device_t *device, audio_stream_out *stream) {
  CHECK(device == &g.device);
  ++g.output_closes;
  g.last_closed_output = stream;
}

int fakeInputOpen(hisi_audio_hw_device_t *device, audio_io_handle_t handle,
                  audio_devices_t devices, audio_config *config,
                  audio_stream_in **stream, audio_input_flags_t flags,
                  const char *address, audio_source_t source) {
  ++g.input_opens;
  CHECK(device == &g.device);
  CHECK(handle == g.expected_handle);
  CHECK(devices == g.expected_devices);
  CHECK(flags == g.expected_input_flags);
  CHECK(config == &g.expected_input_config);
  CHECK(address == g.expected_address);
  CHECK(source == g.expected_source);
  config->sample_rate = 32000;
  config->channel_mask = AUDIO_CHANNEL_IN_MONO;
  config->format = AUDIO_FORMAT_PCM_16_BIT;
  if (g.input_result != 0) {
    if (g.return_input_raw_on_error)
      *stream = &g.input;
    return g.input_result;
  }
  if (g.input_returns_null) {
    *stream = nullptr;
    return 0;
  }
  fillInput(&g.input);
  if (g.break_input_required)
    g.input.read = nullptr;
  *stream = &g.input;
  return 0;
}

void fakeInputClose(hisi_audio_hw_device_t *device, audio_stream_in *stream) {
  CHECK(device == &g.device);
  ++g.input_closes;
  g.last_closed_input = stream;
}

void reset() {
  g = Fixture{};
  g.device.common.tag = HARDWARE_DEVICE_TAG;
  g.device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
  g.device.common.close = fakeDeviceClose;
  g.device.open_output_stream = fakeOutputOpen;
  g.device.close_output_stream = fakeOutputClose;
  g.device.open_input_stream = fakeInputOpen;
  g.device.close_input_stream = fakeInputClose;
  g.methods.open = fakeModuleOpen;
  g.module.methods = &g.methods;
  g.expected_handle = 41;
  g.expected_devices = AUDIO_DEVICE_OUT_SPEAKER;
  g.expected_output_flags =
      static_cast<audio_output_flags_t>(AUDIO_OUTPUT_FLAG_DIRECT);
  g.expected_input_flags =
      static_cast<audio_input_flags_t>(AUDIO_INPUT_FLAG_FAST);
  g.expected_address = "bus-test";
  g.expected_source = AUDIO_SOURCE_MIC;
  g.expected_output_config.sample_rate = 48000;
  g.expected_input_config.sample_rate = 48000;
}

audio_hw_device *openWrapper() {
  hw_device_t *device = nullptr;
  const int status =
      HMI.common.methods->open(&HMI.common, AUDIO_HARDWARE_INTERFACE, &device);
  CHECK(status == 0);
  CHECK(device != nullptr);
  return reinterpret_cast<audio_hw_device *>(device);
}

void closeWrapper(audio_hw_device *device) {
  CHECK(device->common.close(&device->common) == 0);
}

void testProfileRejectsBeforeVendorOpen() {
  reset();
  g.profile_result = -ENODEV;
  hw_device_t *device = nullptr;
  const int status =
      HMI.common.methods->open(&HMI.common, AUDIO_HARDWARE_INTERFACE, &device);
  CHECK(status == -ENODEV);
  CHECK(device == nullptr);
  CHECK(g.profile_checks == 1);
  CHECK(g.module_opens == 0);
}

void testOutputRoundTripAndClose() {
  reset();
  audio_hw_device *device = openWrapper();
  audio_stream_out *stream = nullptr;
  const int status = device->open_output_stream(
      device, g.expected_handle, g.expected_devices, g.expected_output_flags,
      &g.expected_output_config, &stream, g.expected_address);
  CHECK(status == 0 && stream != nullptr);
  CHECK(stream != &g.output[0]);
  CHECK(g.expected_output_config.sample_rate == 44100);
  CHECK(g.expected_output_config.channel_mask == AUDIO_CHANNEL_OUT_MONO);
  CHECK(g.expected_output_config.format == AUDIO_FORMAT_PCM_24_BIT_PACKED);
  CHECK(stream->common.get_sample_rate(&stream->common) == kRateA);
  char data[8]{};
  CHECK(stream->write(stream, data, sizeof(data)) ==
        static_cast<ssize_t>(sizeof(data)));
  uint64_t frames = 0;
  timespec timestamp{};
  CHECK(stream->get_presentation_position != nullptr);
  CHECK(stream->get_presentation_position(stream, &frames, &timestamp) == 0);
  CHECK(stream->get_audio_description_mix_level == nullptr);
  device->close_output_stream(device, stream);
  CHECK(g.output_closes == 1);
  CHECK(g.last_closed_output == &g.output[0]);
  closeWrapper(device);
  CHECK(g.device_closes == 1);
}

void testInputRoundTripAndClose() {
  reset();
  audio_hw_device *device = openWrapper();
  audio_stream_in *stream = nullptr;
  const int status = device->open_input_stream(
      device, g.expected_handle, g.expected_devices, &g.expected_input_config,
      &stream, g.expected_input_flags, g.expected_address, g.expected_source);
  CHECK(status == 0 && stream != nullptr);
  CHECK(stream != &g.input);
  CHECK(g.expected_input_config.sample_rate == 32000);
  CHECK(g.expected_input_config.channel_mask == AUDIO_CHANNEL_IN_MONO);
  CHECK(g.expected_input_config.format == AUDIO_FORMAT_PCM_16_BIT);
  CHECK(stream->common.get_sample_rate(&stream->common) == kRateA);
  char data[8]{};
  CHECK(stream->read(stream, data, sizeof(data)) ==
        static_cast<ssize_t>(sizeof(data)));
  CHECK(stream->set_microphone_direction == nullptr);
  device->close_input_stream(device, stream);
  CHECK(g.input_closes == 1);
  CHECK(g.last_closed_input == &g.input);
  closeWrapper(device);
}

void testVendorErrorsAndOwnership() {
  reset();
  audio_hw_device *device = openWrapper();
  audio_stream_out *out = reinterpret_cast<audio_stream_out *>(uintptr_t{1});
  g.output_result = -EIO;
  g.return_output_raw_on_error = true;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &out,
                                   g.expected_address) == -EIO);
  CHECK(out == nullptr);
  CHECK(g.expected_output_config.sample_rate == 44100);
  CHECK(g.output_closes == 0);
  g.output_result = 1;
  out = nullptr;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &out,
                                   g.expected_address) == 1);
  CHECK(out == nullptr && g.output_closes == 0);

  audio_stream_in *in = reinterpret_cast<audio_stream_in *>(uintptr_t{1});
  g.input_result = -EIO;
  g.return_input_raw_on_error = true;
  CHECK(device->open_input_stream(device, g.expected_handle, g.expected_devices,
                                  &g.expected_input_config, &in,
                                  g.expected_input_flags, g.expected_address,
                                  g.expected_source) == -EIO);
  CHECK(in == nullptr);
  CHECK(g.expected_input_config.sample_rate == 32000);
  CHECK(g.input_closes == 0);
  g.input_result = 1;
  in = nullptr;
  CHECK(device->open_input_stream(device, g.expected_handle, g.expected_devices,
                                  &g.expected_input_config, &in,
                                  g.expected_input_flags, g.expected_address,
                                  g.expected_source) == 1);
  CHECK(in == nullptr && g.input_closes == 0);
  closeWrapper(device);
}

void testNullVendorResultsAndWrapFailure() {
  reset();
  audio_hw_device *device = openWrapper();
  audio_stream_out *out = nullptr;
  g.output_returns_null = true;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &out,
                                   g.expected_address) == -ENODEV);
  CHECK(out == nullptr && g.output_closes == 0);
  g.output_returns_null = false;
  g.break_output_required = true;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &out,
                                   g.expected_address) == -EINVAL);
  CHECK(out == nullptr && g.output_closes == 1);
  CHECK(g.last_closed_output == &g.output[0]);
  g.break_output_required = false;

  audio_stream_in *in = nullptr;
  g.input_returns_null = true;
  CHECK(device->open_input_stream(device, g.expected_handle, g.expected_devices,
                                  &g.expected_input_config, &in,
                                  g.expected_input_flags, g.expected_address,
                                  g.expected_source) == -ENODEV);
  CHECK(in == nullptr && g.input_closes == 0);
  g.input_returns_null = false;
  g.break_input_required = true;
  CHECK(device->open_input_stream(device, g.expected_handle, g.expected_devices,
                                  &g.expected_input_config, &in,
                                  g.expected_input_flags, g.expected_address,
                                  g.expected_source) == -EINVAL);
  CHECK(in == nullptr && g.input_closes == 1);
  CHECK(g.last_closed_input == &g.input);
  closeWrapper(device);
}

void testInvalidOutputArgumentsAndNullCloses() {
  reset();
  audio_hw_device *device = openWrapper();
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, nullptr,
                                   g.expected_address) == -EINVAL);
  CHECK(g.output_opens == 0);
  CHECK(device->open_input_stream(device, g.expected_handle, g.expected_devices,
                                  &g.expected_input_config, nullptr,
                                  g.expected_input_flags, g.expected_address,
                                  g.expected_source) == -EINVAL);
  CHECK(g.input_opens == 0);
  device->close_output_stream(device, nullptr);
  device->close_input_stream(device, nullptr);
  CHECK(g.output_closes == 0 && g.input_closes == 0);
  closeWrapper(device);
}

void testCachedRawStreamGetsFreshCallbacks() {
  reset();
  audio_hw_device *device = openWrapper();
  audio_stream_out *first = nullptr;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &first,
                                   g.expected_address) == 0);
  CHECK(first->common.get_sample_rate(&first->common) == kRateA);
  device->close_output_stream(device, first);
  g.output_rate = kRateB;
  audio_stream_out *second = nullptr;
  CHECK(device->open_output_stream(device, g.expected_handle,
                                   g.expected_devices, g.expected_output_flags,
                                   &g.expected_output_config, &second,
                                   g.expected_address) == 0);
  CHECK(second->common.get_sample_rate(&second->common) == kRateB);
  device->close_output_stream(device, second);
  CHECK(g.output_closes == 2);
  closeWrapper(device);
}

} // namespace

namespace huawei::audio {
int verifyModuleSha256(const hw_module_t *module, const char *expectedHex) {
  CHECK(module == &g.module);
  CHECK(expectedHex != nullptr &&
        std::strcmp(expectedHex, kVerifiedProfileSha256) == 0);
  ++g.profile_checks;
  return g.profile_result;
}
} // namespace huawei::audio

extern "C" int hw_get_module_by_class(const char *classId, const char *instance,
                                      const hw_module_t **module) {
  CHECK(classId != nullptr &&
        std::strcmp(classId, AUDIO_HARDWARE_MODULE_ID) == 0);
  CHECK(instance != nullptr && std::strcmp(instance, "primary_hisi") == 0);
  *module = &g.module;
  return 0;
}

extern "C" int __android_log_print(int, const char *, const char *, ...) {
  return 0;
}

int main() {
  testProfileRejectsBeforeVendorOpen();
  testOutputRoundTripAndClose();
  testInputRoundTripAndClose();
  testVendorErrorsAndOwnership();
  testNullVendorResultsAndWrapFailure();
  testInvalidOutputArgumentsAndNullCloses();
  testCachedRawStreamGetsFreshCallbacks();
  std::puts("audio HAL wrapper integration tests passed");
  return 0;
}
