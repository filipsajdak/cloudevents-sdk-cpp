#!/usr/bin/env bash
# Build a source tree and measure it in the performance job's toolchain.
#
#   bench/perf/run_in_docker.sh [SOURCE_TREE] [OUT_JSON]
#
# SOURCE_TREE defaults to this checkout and OUT_JSON to ./results.json. The
# tree is mounted read-only and built from scratch inside the container, so two
# runs of the same tree measure the same binaries.
#
# CE_PERF_PLATFORM=linux/amd64 builds and runs an x86-64 image under emulation,
# which is what CI measures on. Instruction counts depend on the architecture,
# so only an x86-64 run is comparable with CI's numbers and budgets; an arm64
# run is comparable only with another arm64 run. Emulation is slow, and
# Valgrind under it slower still.
#
# CE_PERF_MEASURE_ARGS passes extra arguments to measure.py, for example
# --no-wall-time.
#
# Both paths are bind-mounted, so they must be visible to the Docker VM; with
# Colima or Docker Desktop that means somewhere under your home directory, not
# /tmp.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
source_tree="$(cd "${1:-$here/../..}" && pwd)"
out="${2:-results.json}"
out_dir="$(cd "$(dirname "$out")" && pwd)"
out_name="$(basename "$out")"

platform="${CE_PERF_PLATFORM:-}"
image="ce-perf"
platform_args=()
if [ -n "$platform" ]; then
  image="ce-perf-${platform//\//-}"
  platform_args=(--platform "$platform")
fi

docker build ${platform_args[@]+"${platform_args[@]}"} -t "$image" "$here"

docker run --rm ${platform_args[@]+"${platform_args[@]}"} \
  -v "$source_tree:/src:ro" -v "$out_dir:/out" \
  -e CE_PERF_MEASURE_ARGS="${CE_PERF_MEASURE_ARGS:-}" \
  -e CE_PERF_OUT="/out/$out_name" \
  "$image" bash -euo pipefail -c '
    # shellcheck disable=SC2086
    /src/bench/perf/build_and_measure.sh /src /build "$CE_PERF_OUT" $CE_PERF_MEASURE_ARGS
  '
echo "results in $out"
