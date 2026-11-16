#!/usr/bin/env python3
"""Build and run the audio stream adapter against the repository's real headers."""

import json
import os
import pathlib
import resource
import shlex
import subprocess
import sys
from datetime import datetime, timezone
import uuid


ROOT = pathlib.Path(__file__).resolve().parents[6]
STREAM = ROOT / "device/huawei/compat/audio/stream"
TEST = STREAM / "tests/StreamAdapterHostTest.cpp"
IMPL = STREAM / "StreamAdapter.cpp"
DEFAULT_OUT = ROOT / "out/audio-stream-prototype/host-tests"
CLANG = ROOT / "prebuilts/clang/host/linux-x86/clang-r536225/bin/clang++"
COMPILER = shlex.split(os.environ.get("AUDIO_STREAM_CXX", str(CLANG)))
INCLUDES = [
    "hardware/libhardware/include_all",
    "system/media/audio/include",
    "system/core/libcutils/include",
    "system/core/libsystem/include",
]


def no_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("AUDIO_STREAM_TEST_OUT", DEFAULT_OUT)))
    args = parser.parse_args()
    evidence_root = args.out_dir if args.out_dir.is_absolute() else ROOT / args.out_dir
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    OUT = evidence_root / run_id
    OUT.mkdir(parents=True, exist_ok=True)
    result = {"test": str(TEST), "implementation": str(IMPL), "root": str(ROOT),
              "run_id": run_id, "evidence_dir": str(OUT)}
    if not TEST.is_file() or not IMPL.is_file():
        result.update({"result": "BLOCKED", "reason": "test or implementation source is not present"})
        (OUT / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        (evidence_root / "latest.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 2

    executable = OUT / "audio-stream-host-test"
    command = COMPILER + [
        "-std=c++17",
        "-O1",
        "-g",
        "-pthread",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer",
    ]
    for include in INCLUDES:
        command += ["-isystem", str(ROOT / include)]
    command += ["-I", str(STREAM / "include"), str(IMPL), str(TEST),
                "-o", str(executable)]
    result["compile_command"] = command
    compile_run = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
    (OUT / "compile.log").write_text(compile_run.stdout)
    result["compile_exit_code"] = compile_run.returncode
    if compile_run.returncode:
        result["result"] = "FAIL_COMPILE"
        (OUT / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        (evidence_root / "latest.json").write_text(json.dumps(result, indent=2) + "\n")
        print(compile_run.stdout)
        return compile_run.returncode

    run_command = [str(executable)]
    result["run_command"] = run_command
    environment = os.environ.copy()
    environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=1")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    result["sanitizers"] = {
        "compile": "-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer",
        "threading": "pthread_create/pthread_join host fixture; no std::thread runtime dependency",
        "ASAN_OPTIONS": environment["ASAN_OPTIONS"],
        "UBSAN_OPTIONS": environment["UBSAN_OPTIONS"],
    }
    run = subprocess.run(run_command, cwd=ROOT, env=environment, text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         preexec_fn=no_core_dumps)
    (OUT / "run.log").write_text(run.stdout)
    result["run_exit_code"] = run.returncode
    result["result"] = "PASS" if run.returncode == 0 else "FAIL_RUN"
    (OUT / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    (evidence_root / "latest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(run.stdout)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
