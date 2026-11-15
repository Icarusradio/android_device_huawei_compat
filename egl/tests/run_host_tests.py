#!/usr/bin/env python3
"""Build and exercise the EGL wrapper against real host shared libraries."""
import os
import pathlib
import platform
import re
import resource
import shlex
import signal
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[5]
SRC = ROOT / "device/huawei/compat/egl"
TESTS = SRC / "tests"
OUT = ROOT / "out/egl-wrapper-prototype/tests"
INCLUDE = ROOT / "frameworks/native/opengl/include"
GENERATOR = SRC / "generate_driver_symbols.py"
CC = shlex.split(os.environ.get("CXX", "g++"))
READELF = os.environ.get("READELF", "readelf")


def run(command, **kwargs):
    print("+", shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def exports(path):
    out = subprocess.check_output([READELF, "--dyn-syms", "--wide", str(path)], text=True)
    result = set()
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 8 and re.fullmatch(r"(?:egl|gl)[A-Z][A-Za-z0-9_]*", fields[7].split("@", 1)[0]):
            if fields[3] == "FUNC" and fields[4] == "GLOBAL" and fields[5] == "DEFAULT" and fields[6] != "UND":
                result.add(fields[7].split("@", 1)[0])
    return result


def no_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    fixture_symbols = OUT / "DriverSymbols.fixture.inc"
    run([sys.executable, GENERATOR, fixture_symbols, "--fixture"])

    fixture = OUT / "libfake-egl-driver.so"
    run(CC + ["-std=c++17", "-fPIC", "-fvisibility=hidden", "-shared", "-Wl,-Bsymbolic",
              "-I", INCLUDE, TESTS / "fake_driver.cpp", "-o", fixture])
    wrapper_sources = [SRC / "Wrapper.cpp", SRC / "Guard.S", SRC / "Forwarders.S"]
    missing = [path for path in wrapper_sources if not path.exists()]
    if missing:
        raise SystemExit("wrapper implementation sources not ready: " + ", ".join(map(str, missing)))

    wrapper = OUT / "libGLES_huawei_compat.so"

    def build_wrapper(destination, driver):
        run(CC + ["-std=c++17", "-fPIC", "-fvisibility=hidden", "-shared", "-pthread",
                  "-Wl,-Bsymbolic", "-I", INCLUDE,
                  f'-DHUAWEI_EGL_SYMBOLS_FILE="{fixture_symbols}"',
                  f'-DHUAWEI_EGL_DRIVER_PATH="{driver}"', *wrapper_sources,
                  "-ldl", "-o", destination])

    build_wrapper(wrapper, fixture)
    runner = OUT / "egl-wrapper-host-runner"
    run(CC + ["-std=c++17", "-pthread", "-rdynamic", "-I", INCLUDE,
              TESTS / "host_runner.cpp", "-ldl", "-o", runner])

    wrapper_exports, driver_exports = exports(wrapper), exports(fixture)
    if wrapper_exports != driver_exports:
        raise SystemExit("EGL/GL dynamic exports differ: wrapper-only=" +
                         repr(sorted(wrapper_exports-driver_exports)) + " driver-only=" +
                         repr(sorted(driver_exports-wrapper_exports)))
    print(f"matching EGL/GL exports: {len(wrapper_exports)} symbols", flush=True)
    run([runner, wrapper, fixture])

    # Fail-fast behavior is asserted in subprocesses so SIGABRT cannot kill this harness.
    probe = OUT / "egl-wrapper-crash-probe"
    run(CC + ["-std=c++17", "-rdynamic", TESTS / "crash_probe.cpp", "-ldl", "-o", probe])
    absent_driver_wrapper = OUT / "libGLES_huawei_compat_missing_driver.so"
    build_wrapper(absent_driver_wrapper, OUT / "no-such-egl-driver.so")
    missing_export_driver = OUT / "libfake-egl-driver-missing-export.so"
    # Rebuild the source with the omission switch defined by the fixture.
    run(CC + ["-std=c++17", "-fPIC", "-fvisibility=hidden", "-shared", "-Wl,-Bsymbolic",
              "-I", INCLUDE,
              "-DEGL_TEST_OMIT_RAW_MIXED", TESTS / "fake_driver.cpp", "-o", missing_export_driver])
    missing_export_wrapper = OUT / "libGLES_huawei_compat_missing_export.so"
    build_wrapper(missing_export_wrapper, missing_export_driver)
    for target, text in ((absent_driver_wrapper, "dlopen failed"),
                         (missing_export_wrapper, "glRawMixed")):
        result = subprocess.run([probe, target], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, preexec_fn=no_core_dumps)
        if result.returncode != -signal.SIGABRT or text not in result.stderr:
            raise SystemExit(f"expected SIGABRT and {text!r}; rc={result.returncode}; "
                             f"stderr={result.stderr!r}")
        print(f"confirmed SIGABRT with {text!r} diagnostic", flush=True)

    if platform.machine().lower() in ("aarch64", "arm64"):
        x18_runner = OUT / "x18-host-runner"
        run(CC + ["-std=c++17", TESTS / "x18_host_runner.cpp", TESTS / "x18_probe.S",
                  SRC / "Guard.S", "-o", x18_runner])
        run([x18_runner])
        print("AArch64 x18 mechanism checks passed (guarded and unguarded target)", flush=True)
    else:
        clang = ROOT / "prebuilts/clang/host/linux-x86/clang-r536225/bin/clang"
        if clang.exists():
            for source, output in ((TESTS / "x18_probe.S", OUT / "x18_probe.aarch64.o"),
                                   (SRC / "Guard.S", OUT / "guard.aarch64.o"),
                                   (TESTS / "x18_host_runner.cpp", OUT / "x18_host_runner.aarch64.o")):
                run([clang, "--target=aarch64-linux-android", "-c", source, "-o", output])
            print("AArch64 x18 sources cross-compiled; runtime checks: SKIP (host is not AArch64)",
                  flush=True)
        else:
            print("AArch64 x18 runtime checks: SKIP (clang r536225 not found; no host emulation)",
                  flush=True)


if __name__ == "__main__":
    main()
