#!/usr/bin/env python3
"""Take every measurement the performance job gates on or reports, for one build.

    measure.py --build-dir build/perf --out results.json

The build directory must hold `bench/perf/perf_probe`, `perf_probe_instr` and
the four `bench/perf/perf_consumer_<codec>` binaries (configure with
-DCE_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release). `bench/codec_bench` is
optional: its wall and CPU times are recorded when it exists, and never gate.

Instruction counts need Valgrind. `--no-instructions` skips them, for a quick
local look on a machine without it; the results then cannot be compared with
a run that has them.

Standard library only, so it runs on a bare CI image.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCHEMA = 1

# Iterations inside the measured loop. Callgrind runs 20 to 50 times slower
# than native, so these keep the whole run to a few minutes while leaving the
# loop's instructions far above Valgrind's per-process noise. The count per
# operation is the loop's total divided by these, so they may change without
# moving any result.
INSTR_ITERATIONS = {
    "decode_minimal": 2000,
    "decode_full": 1000,
    "decode_large": 20,
    "encode_full": 1000,
    "roundtrip_full": 500,
    "decode_batch_100": 20,
    "encode_batch_100": 20,
    "typed_payload_read": 2000,
    "typed_payload_read_document": 2000,
    "typed_payload_write": 2000,
    "decode_as_full": 1000,
    "decode_as_large": 20,
    "decode_batch_as_100": 20,
    "encode_as_full": 1000,
    "http_decode_binary": 1000,
    "http_encode_binary": 1000,
    "http_decode_structured": 1000,
    "kafka_decode_binary": 1000,
    "nats_decode_binary": 1000,
}
DEFAULT_ITERATIONS = 200

CODECS = ("nlohmann", "rapidjson", "boost.json", "glaze")


class MeasureFailure(Exception):
    """One measurement that could not be taken: the operation id, the mode it
    was measured in, and what the failing tool said."""

    def __init__(self, op_id: str, mode: str, message: str) -> None:
        super().__init__(f"measure: {mode} {op_id} failed:\n{message}")
        self.op_id = op_id
        self.mode = mode
        self.message = message

    def as_json(self) -> dict[str, str]:
        return {"id": self.op_id, "mode": self.mode, "message": self.message}


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kwargs)


def probe_json(probe: Path, mode: str, op_id: str = "") -> dict:
    args = [mode, op_id] if op_id else [mode]
    try:
        out = run([str(probe), *args]).stdout
    except subprocess.CalledProcessError as exc:
        raise MeasureFailure(op_id, f"perf_probe {mode}", exc.stderr.strip()) from exc
    return json.loads(out)


def first_line(cmd: list[str]) -> str:
    try:
        return run(cmd).stdout.splitlines()[0].strip()
    except (OSError, subprocess.CalledProcessError, IndexError):
        return "unknown"


def compiler_of(build_dir: Path) -> tuple[str, str]:
    """The compiler's --version line, and a short id such as 'gcc-14' that
    changes only when instruction counts would."""
    cache = build_dir / "CMakeCache.txt"
    compiler = "c++"
    if cache.is_file():
        found = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.+)$", cache.read_text(), re.MULTILINE)
        if found:
            compiler = found.group(1).strip()
    version = first_line([compiler, "--version"])
    family = "clang" if "clang" in version.lower() else "gcc"
    major = re.search(r"version (\d+)\.", version) or re.search(r"(\d+)\.\d+\.\d+\s*$", version)
    return version, f"{family}-{major.group(1) if major else 'unknown'}"


def git_sha(source_dir: Path) -> str:
    try:
        return run(["git", "-c", "safe.directory=*", "-C", str(source_dir),
                    "rev-parse", "HEAD"]).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def callgrind_instructions(probe: Path, op_id: str, iterations: int, scratch: Path) -> int:
    out_file = scratch / "callgrind.out"
    cmd = [
        "valgrind", "--tool=callgrind", "--collect-atstart=no",
        "--toggle-collect=ce_perf_measured_loop*",
        f"--callgrind-out-file={out_file}",
        str(probe), "instr", op_id, str(iterations),
    ]
    try:
        run(cmd)
    except subprocess.CalledProcessError as exc:
        raise MeasureFailure(op_id, "callgrind", exc.stderr.strip()) from exc
    text = out_file.read_text()
    found = re.search(r"^(?:summary|totals):\s+(\d+)", text, re.MULTILINE)
    if not found or int(found.group(1)) == 0:
        raise MeasureFailure(op_id, "callgrind",
                             "counted nothing; the toggle did not match ce_perf_measured_loop")
    total = int(found.group(1))
    return round(total / iterations)


def stripped_size(binary: Path, scratch: Path) -> int:
    copy = scratch / binary.name
    shutil.copyfile(binary, copy)
    run(["strip", str(copy)])
    return copy.stat().st_size


def benchmark_times(bench: Path) -> dict[str, dict[str, float]]:
    """Wall and CPU nanoseconds per operation from Google Benchmark. Reported,
    never gated: shared runners vary by more than the gates' thresholds."""
    try:
        out = run([str(bench), "--benchmark_format=json",
                   "--benchmark_min_time=0.2s"]).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"measure: codec_bench did not run ({exc}); wall times omitted", file=sys.stderr)
        return {}
    scale = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
    times: dict[str, dict[str, float]] = {}
    for entry in json.loads(out).get("benchmarks", []):
        factor = scale.get(entry.get("time_unit", "ns"), 1.0)
        times[entry["name"]] = {
            "wall_ns": round(entry["real_time"] * factor, 1),
            "cpu_ns": round(entry["cpu_time"] * factor, 1),
        }
    return times


def measure(build_dir: Path, source_dir: Path, instructions: bool, wall: bool) -> dict:
    perf_dir = build_dir / "bench" / "perf"
    probe = perf_dir / "perf_probe"
    if not probe.is_file():
        sys.exit(f"measure: {probe} not found; build the perf_probe target first")

    # Instructions come from a probe with no allocation accounting, so they
    # measure the SDK and the codec alone. A build from before that probe
    # existed has only perf_probe, which is then used for both.
    instr_probe = perf_dir / "perf_probe_instr"
    if not instr_probe.is_file():
        instr_probe = probe

    listing = probe_json(probe, "list")
    measurements: dict[str, dict[str, float | int]] = {}

    with tempfile.TemporaryDirectory() as tmp:
        scratch = Path(tmp)
        for op_id in listing["operations"]:
            entry = measurements.setdefault(op_id, {})
            counted = probe_json(probe, "alloc", op_id)
            entry["allocations"] = counted["allocations"]
            entry["allocated_bytes"] = counted["allocated_bytes"]
            if op_id in listing["retained"]:
                entry["retained_bytes"] = probe_json(probe, "retained", op_id)["retained_bytes"]
            if instructions:
                op = op_id.split("/", 1)[0]
                iterations = INSTR_ITERATIONS.get(op, DEFAULT_ITERATIONS)
                entry["instructions"] = callgrind_instructions(instr_probe, op_id, iterations,
                                                             scratch)
            print(f"measured {op_id}", file=sys.stderr)

        for codec in CODECS:
            binary = perf_dir / f"perf_consumer_{codec.replace('.', '_')}"
            if binary.is_file():
                measurements[f"consumer/{codec}"] = {"binary_bytes": stripped_size(binary, scratch)}

    if wall:
        bench = build_dir / "bench" / "codec_bench"
        if bench.is_file():
            for name, times in benchmark_times(bench).items():
                if name in measurements:
                    measurements[name].update(times)

    compiler, compiler_id = compiler_of(build_dir)
    return {
        "schema": SCHEMA,
        "git_sha": git_sha(source_dir),
        "date": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "toolchain": {
            "arch": platform.machine(),
            "system": platform.system(),
            "compiler": compiler,
            "compiler_id": compiler_id,
            "valgrind": first_line(["valgrind", "--version"]) if instructions else "not used",
            "counts_malloc": listing.get("counts_malloc", False),
        },
        "measurements": dict(sorted(measurements.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2],
                        help="the tree the build came from, for its git sha")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--no-instructions", action="store_true",
                        help="skip the Callgrind counts (no Valgrind on this machine)")
    parser.add_argument("--no-wall-time", action="store_true",
                        help="skip Google Benchmark")
    parser.add_argument("--failure", type=Path,
                        help="when a measurement fails, write its id, mode and message here "
                             "as JSON, for compare.py --measure-failure to report")
    args = parser.parse_args()

    try:
        results = measure(args.build_dir.resolve(), args.source_dir.resolve(),
                          instructions=not args.no_instructions, wall=not args.no_wall_time)
    except MeasureFailure as failure:
        if args.failure is not None:
            args.failure.write_text(json.dumps(failure.as_json(), indent=2) + "\n")
        print(failure, file=sys.stderr)
        return 1
    args.out.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(f"measure: {len(results['measurements'])} ids written to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
