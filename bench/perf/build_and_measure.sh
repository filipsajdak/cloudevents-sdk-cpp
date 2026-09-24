#!/usr/bin/env bash
# Configure, build and measure one source tree the way the performance job does.
#
#   bench/perf/build_and_measure.sh SOURCE_TREE BUILD_DIR OUT_JSON [measure.py args...]
#
# The CI job and run_in_docker.sh both call this, so the flags a local
# reproduction builds with are the ones CI builds with. measure.py is taken
# from beside this script, not from SOURCE_TREE: when the job measures main
# with a pull request's harness, the driver is the pull request's too.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
source_tree="$1"
build_dir="$2"
out="$3"
shift 3

cmake -S "$source_tree" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="${CXX:-g++-14}" \
  -DCE_BUILD_BENCHMARKS=ON \
  -DCE_BUILD_TESTING=OFF
cmake --build "$build_dir" --target perf_measure
python3 "$here/measure.py" --build-dir "$build_dir" --source-dir "$source_tree" --out "$out" "$@"
