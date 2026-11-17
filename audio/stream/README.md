# Huawei audio stream compatibility adapter

This static library is selected only by mediapadm5lte's 64-bit audio wrapper.
The product enables the adapter behind the `huaweiAudioVars` Soong bool
`mediapadm5lte_stream_compat`; it remains disabled by default and is not enabled
for other products or 32-bit builds. mediapadm5lte now selects the official
HIDL 6.0 audio core and effects implementations. The adapter itself does not
change the C stream ABI, SHA profile, ownership rules, or 32-bit path. On the
HIDL 4.0 base, the user confirmed ordinary playback and recording work. Runtime
validation after flashing the HIDL 6.0 product is still pending. At runtime the
HAL wrapper identifies the loaded module against one known 64-bit Huawei
Hi3660 audio primary blob profile:

`5c882f570fee9778167f3d5a2e2f704892ec426f15b50f345c546cbf42005724`

The SHA-256 check is an ABI profile gate, not a signature or authenticity
check. If the module cannot be resolved, read, or matched, the HAL refuses to
open it and does not fall back to exposing the raw legacy stream.

The wrapper gives the HIDL 4.0 caller a fully initialized modern stream and
keeps a separate snapshot of only the legacy callback-table prefix confirmed
in the blob. Calls through the modern table forward the original legacy stream
pointer as `self`; the adapter never expands or rewrites the vendor-owned
allocation. The output prefix ends after
`get_presentation_position` at byte 200. The input prefix ends after
`get_active_microphones` at byte 184. The prefix table is expected to remain
stable between stream open and close.

Run the isolated host contract tests with:

```sh
python3 device/huawei/compat/audio/stream/tests/run_host_tests.py
```

On a successful wrap, the wrapper owns only its adapter allocation. Closing
calls the supplied device close callback with the original stream pointer and
cookie exactly once, then frees the adapter. The vendor close implementation
decides whether to free or cache its stream allocation; the adapter never
frees that allocation itself. If wrapping fails, the caller retains ownership
and must close the original stream. A null wrapped pointer may be closed as a
no-op.

Modern callbacks beyond those verified prefixes remain null. The mediapadm5lte
product selects the official HIDL 6.0 core and effects paths; the adapter does
not add version-specific stream layout changes. In particular, the blob writes
private state at output offset `0x110`, which overlaps the modern
`get_audio_description_mix_level` callback. The wrapper does not expose that
field. Input metadata and newer microphone controls are also outside the
verified prefix. The microphone record passed through the existing callback
matches this build's system header size and channel-mapping offset, although
the semantics of every record field have not been validated.

The adapter passes the existing `audio_config` pointer unchanged. Its
offload-related version and size semantics remain unvalidated on device. The
known silent offload case is a separate metadata issue: the original HAL
requires bitrate and duration information that the current offload AudioTrack
path supplies as zero. This change does not address that issue, and offload
validation is not a prerequisite for withdrawing the two platform HIDL 4.0
discovery patches. Keep offload disabled for ordinary use until it is fixed.
This profile does not apply automatically to another ROM, blob, or 32-bit
build.

For normal synchronous AudioFlinger FMQ operations, the client waits for each
HAL reply. AudioFlinger exits and joins its I/O thread before destroying the
HIDL stream, so those operations finish before close. The HIDL 6.0 service
then stops and wakes its FMQ worker, calls vendor close, and joins that worker
during object destruction. The worker's wake with no queued command does not
make another C stream call. This is the expected normal client shutdown path;
exceptional concurrent shutdown, process death, and real-device stream stress
have not been tested. HIDL 6.0 playback, capture, and repeated stream lifecycle
validation after flashing remain pending. The user-confirmed HIDL 4.0 ordinary
playback and recording results do not cover that HIDL 6.0 runtime validation.
