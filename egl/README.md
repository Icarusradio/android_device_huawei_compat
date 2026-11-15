# Huawei EGL compatibility wrapper

`mediapadm5lte` selects this vendor library through
`ro.hardware.egl=huawei_compat`. The EGL loader checks
`persist.graphics.egl` before `ro.hardware.egl`, so the product's persistent
setting must be empty or use the same suffix for this selection to take effect.
The common hi3660 vendor property keeps `mali` as its optional default so other
products retain their current driver choice. The wrapper remains a normal
platform EGL vendor driver selected by the existing loader; it explicitly loads
the original Mali blob, which remains part of the product.

The checked-in forwarding list and version script are tied to the reviewed
32-bit and 64-bit `libGLES_mali.so` inputs. The generator checks each input
SHA-256 and requires identical export sets. Use `--check` to verify both files
without changing them. `--fixture` is only for the standalone host ABI fixture.
From the repository root, verify the checked-in inputs and generated files with:

```sh
python3 device/huawei/compat/egl/generate_driver_symbols.py \
    device/huawei/compat/egl/DriverSymbols.inc --check
```

The host fixture suite can be rebuilt and run with:

```sh
python3 device/huawei/compat/egl/tests/run_host_tests.py
```

It requires Python 3, `g++`, `readelf`, and the repository's bundled AOSP Clang
at `prebuilts/clang/host/linux-x86/clang-r536225/bin/clang` for the AArch64
cross-compilation checks. The recorded run is in
`out/egl-wrapper-prototype/tests/host-test.log` (exit status 0). It covers
fixture export parity, host `dlopen`/`dlclose` lifecycle and reentry, query
mapping, ABI forwarding, concurrent queries, and expected fail-fast cases.

The standalone AArch64 x18 guard probe is implemented by
`device/huawei/compat/egl/tests/x18_host_runner.cpp` and
`device/huawei/compat/egl/tests/x18_probe.S`; `run_host_tests.py` builds those
sources together with `Guard.S`. On the x86_64 host it cross-compiles but cannot
execute. A separate Android ARM64 probe was run on CMR-AL09: the raw Mali load
path reproduced an x18 clear, while the wrapper preserved sampled x18 through
all four tested fresh-process entry sequences. A separate ARM32 device probe
passed the load/query/display-handle checks. The detailed evidence is in the
2026-11-15 EGL device-boundary validation record. These tests establish the
guarded loader boundary for the tested blobs and entry paths; they do not cover
the product's system Loader selection, zygote/HWUI order, or full rendering.

The wrapper explicitly opens the architecture-matched Mali library and resolves
its exported functions. A missing driver or required symbol prints a diagnostic
and aborts; equivalence with every platform loader fallback path has not been
established. `eglGetProcAddress` remains a call to the real driver query, with
null results preserved and selected display entry points wrapped.
Once a protected display target is queried, later queries for that name must
return the same address; an address change fails fast because existing callers
may already hold the hidden wrapper. An alias is wrapped only when its address
matches a canonical protected target already known to the wrapper; unknown
aliases are returned as the driver supplied them.

Only EGL lookup/display calls cross the register guard. Ordinary GL exports
are tail-forwarded and their driver calls, callbacks, and transitive runtime
behavior are outside this wrapper's protection boundary. No claim is made
that matching exports alone captures the driver's complete dependency closure.
A different or updated vendor blob must be re-audited for its SHA-256, export
set, and runtime behavior before this forwarding list is reused. The CMR-AL09
product integration passed the user's reported checks with the platform HWUI
x18 guard still present; that guard has since been removed from frameworks/base.
The earlier device A/B ran with SELinux in Permissive mode. Boot, loader and
rendering behavior with the guard removed must be validated on the resulting build.
