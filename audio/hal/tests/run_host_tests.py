#!/usr/bin/env python3
"""Build the Huawei audio wrapper and module profile against Android headers."""

import hashlib
import argparse
import json
import os
import pathlib
import resource
import subprocess
import sys
import uuid
from datetime import datetime, timezone


ROOT = pathlib.Path(__file__).resolve().parents[6]
HAL = ROOT / "device/huawei/compat/audio/hal"
STREAM = ROOT / "device/huawei/compat/audio/stream"
DEFAULT_OUT_ROOT = ROOT / "out/audio-hal-compat/host-tests"
CLANG = ROOT / "prebuilts/clang/host/linux-x86/clang-r536225/bin/clang++"
CRYPTO_DIR = ROOT / "out/host/linux-x86/lib64"
CRYPTO = CRYPTO_DIR / "libcrypto-host.so"
BASE_FLAGS = [
    "-std=c++17", "-O1", "-g", "-pthread", "-Wall", "-Wextra", "-Werror",
    "-Wno-unused-parameter", "-fsanitize=address,undefined",
    "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
]
# These diagnostics are already present in unrelated legacy portions of audio_hw.cpp.
# Keep them visible while avoiding turning those pre-existing warnings into test failures.
WRAPPER_FLAGS = ["-Wno-error=sign-compare", "-Wno-error=missing-field-initializers"]
INCLUDES = [
    ROOT / "hardware/libhardware/include_all",
    ROOT / "hardware/libhardware/include",
    ROOT / "system/media/audio/include",
    ROOT / "system/core/libcutils/include",
    ROOT / "system/core/include",
    ROOT / "system/logging/liblog/include",
    ROOT / "system/logging/liblog/include_vndk",
]


def no_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(command, cwd, env=None):
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          preexec_fn=no_core_dumps)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("AUDIO_HAL_TEST_OUT", DEFAULT_OUT_ROOT)))
    args = parser.parse_args()
    out_root = args.out_dir if args.out_dir.is_absolute() else ROOT / args.out_dir
    out = out_root / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    out.mkdir(parents=True, exist_ok=False)
    result = {"run_id": out.name, "evidence_dir": str(out), "compiler": str(CLANG),
              "out_root": str(out_root),
              "provider": {"name": str(CRYPTO), "type": "Soong host BoringSSL libcrypto-host",
                           "sha256": None},
              "sanitizers": {"compile": "-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer",
                             "ASAN_OPTIONS": "abort_on_error=1:detect_leaks=1",
                             "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"},
              "tests": []}
    if not CLANG.is_file() or not CRYPTO.is_file():
        result.update({"result": "BLOCKED", "reason": "AOSP clang or prebuilt host BoringSSL library is missing"})
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 2
    result["provider"]["sha256"] = hashlib.sha256(CRYPTO.read_bytes()).hexdigest()

    common = [str(CLANG), *BASE_FLAGS]
    for include in INCLUDES:
        common += ["-isystem", str(include)]

    wrapper = out / "audio-hal-wrapper-host-test"
    wrapper_command = common + WRAPPER_FLAGS + [
        "-fno-exceptions", "-DEMUI_VERSION=9", "-DHUAWEI_AUDIO_STREAM_COMPAT=1",
        "-I" + str(HAL), "-I" + str(STREAM / "include"),
        str(HAL / "audio_hw.cpp"), str(STREAM / "StreamAdapter.cpp"),
        str(HAL / "tests/AudioHalWrapperHostTest.cpp"), "-ldl", "-o", str(wrapper),
    ]
    wrapper_build = run(wrapper_command, ROOT)
    (out / "audio-hal-wrapper-build.log").write_text(wrapper_build.stdout)
    result["tests"].append({"name": "audio_hal_wrapper", "command": wrapper_command,
                            "compile_exit_code": wrapper_build.returncode,
                            "visible_legacy_warnings": wrapper_build.stdout})
    if wrapper_build.returncode:
        result["result"] = "FAIL_COMPILE_WRAPPER"
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(wrapper_build.stdout)
        return wrapper_build.returncode

    sources = [HAL / "audio_hw.cpp", STREAM / "StreamAdapter.cpp",
               HAL / "tests/AudioHalWrapperHostTest.cpp", HAL / "ModuleProfile.cpp",
               HAL / "tests/ModuleProfileHostTest.cpp"]
    result["source_sha256"] = {
        str(source.relative_to(ROOT)): hashlib.sha256(source.read_bytes()).hexdigest()
        for source in sources
    }

    profile = out / "module-profile-host-test"
    profile_command = common + [
        "-I" + str(HAL), "-I" + str(ROOT / "external/boringssl/src/include"),
        str(HAL / "ModuleProfile.cpp"), str(HAL / "tests/ModuleProfileHostTest.cpp"),
        "-L" + str(CRYPTO_DIR), "-lcrypto-host", "-ldl",
        "-Wl,-rpath," + str(CRYPTO_DIR), "-Wl,--export-dynamic", "-o", str(profile),
    ]
    profile_build = run(profile_command, ROOT)
    (out / "module-profile-build.log").write_text(profile_build.stdout)
    result["tests"].append({"name": "module_profile_sha256", "command": profile_command,
                            "compile_exit_code": profile_build.returncode})
    if profile_build.returncode:
        result["result"] = "FAIL_COMPILE_PROFILE"
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(profile_build.stdout)
        return profile_build.returncode

    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    env["LD_LIBRARY_PATH"] = str(CRYPTO_DIR) + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")

    for name, command in [
        ("audio_hal_wrapper", [str(wrapper)]),
        ("module_profile_sha256", [str(profile), hashlib.sha256(profile.read_bytes()).hexdigest()]),
    ]:
        test_run = run(command, ROOT, env)
        (out / (name + ".run.log")).write_text(test_run.stdout)
        entry = next(item for item in result["tests"] if item["name"] == name)
        entry.update({"run_command": command, "run_exit_code": test_run.returncode,
                      "result": "PASS" if test_run.returncode == 0 else "FAIL_RUN"})
        if test_run.returncode:
            result["result"] = "FAIL_RUN_" + name.upper()
            (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            print(test_run.stdout)
            return test_run.returncode

    result["result"] = "PASS"
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print((out / "audio_hal_wrapper.run.log").read_text(), end="")
    print((out / "module_profile_sha256.run.log").read_text(), end="")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
