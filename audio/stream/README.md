# Huawei audio stream compatibility prototype

This static library is an unselected prototype. Nothing in the current product
depends on it. It accepts one verified stream callback-table profile from the
64-bit Huawei Hi3660 audio primary blob:

`5c882f570fee9778167f3d5a2e2f704892ec426f15b50f345c546cbf42005724`

The wrapper gives the system adapter a fully initialized modern stream and
keeps a separate snapshot of only the legacy callback-table prefix that was
confirmed in the blob. Calls through the modern table forward the original
legacy stream pointer as `self`; the adapter never expands or rewrites the
vendor-owned allocation. The output prefix ends after
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

Modern callbacks beyond those verified prefixes remain null, allowing HIDL 6
to report the optional operations as unsupported. In particular, the blob
writes private state at output offset `0x110`, which overlaps the modern
`get_audio_description_mix_level` callback. The wrapper does not expose that
field. Input metadata and newer microphone controls are also outside the
verified prefix. The microphone record passed through the existing callback
matches this build's system header size and channel-mapping offset, although
the semantics of every record field have not been validated.

This module does not identify or load the blob at runtime, attach to a product,
change the HIDL service, or adapt device-open configuration structures. Before
any product integration, the caller must confirm the blob SHA, preserve the
open/close ownership contract, verify callback-table stability, and test the
audio configuration ABI. This profile does not apply automatically to another
ROM, blob, or 32-bit build. No real HIDL 6 service has been tested with it.

The current caller must stop external stream operations before calling the
adapter's close function. Its device close callback must finish any callback
thread cleanup it owns before returning. Upstream HIDL 6 calls device close
before joining its FMQ thread, so this adapter prototype alone does not prove
that the HIDL 6 close sequence is safe; product integration must separately
verify that ordering and any in-flight operations.
