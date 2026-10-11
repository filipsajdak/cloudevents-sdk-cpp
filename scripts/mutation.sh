#!/usr/bin/env bash
# Mutation testing with Mull, restricted to the lines changed since <base-ref>.
#
#   scripts/mutation.sh <base-ref> [--suites a_test,b_test] [--dry-run]
#
# Builds every suite (or the named ones) with the Mull plugin in build/mutation,
# runs mull-runner on each, and folds the results into one verification line
# (scripts/mutation_summary.py). Mull's git diff filter keeps only the mutants on
# lines that differ from the merge base of <base-ref> and HEAD, uncommitted edits
# included. See CONTRIBUTING.md, "Mutation testing".
#
# The compiler must be the LLVM major version the Mull build was made for, so this
# does not use the clang-cxx20 preset's `clang++`. Defaults for macOS (Homebrew
# `llvm@21`, Mull unpacked under ~/.local/opt); override for Linux (apt `mull-21`):
#
#   CE_MULL_LLVM     LLVM prefix (bin/clang++)       /opt/homebrew/opt/llvm@21
#   CE_MULL_DIR      Mull prefix (bin, lib)          ~/.local/opt/mull-21
#   CE_MULL_VERSION  Mull's LLVM major version       21
#   CE_MULL_BUILD    build directory                 build/mutation

set -euo pipefail

usage() {
  echo "usage: scripts/mutation.sh <base-ref> [--suites a_test,b_test] [--dry-run]" >&2
  exit 2
}

[[ $# -ge 1 && $1 != -* ]] || usage
base_ref=$1
shift
suites=""
dry_run=0
while [[ $# -gt 0 ]]; do
  case $1 in
    --suites) [[ $# -ge 2 ]] || usage; suites=$2; shift 2 ;;
    --dry-run) dry_run=1; shift ;;
    *) usage ;;
  esac
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=${CE_MULL_VERSION:-21}
llvm=${CE_MULL_LLVM:-/opt/homebrew/opt/llvm@$version}
mull=${CE_MULL_DIR:-$HOME/.local/opt/mull-$version}
build=${CE_MULL_BUILD:-$root/build/mutation}
runner=$mull/bin/mull-runner-$version
plugin=$mull/lib/mull-ir-frontend-$version

merge_base=$(git -C "$root" merge-base "$base_ref" HEAD)

# The config Mull reads: the committed mull.yml plus the diff filter.
config() {
  cat "$root/mull.yml"
  echo "gitDiffRef: $merge_base"
  echo "gitProjectRoot: $root"
}

if [[ $dry_run -eq 1 ]]; then
  echo "base ref:   $base_ref (merge base $merge_base)"
  echo "compiler:   $llvm/bin/clang++"
  echo "plugin:     $plugin"
  echo "build dir:  $build"
  echo "suites:     ${suites:-all}"
  echo "--- $build/mull.yml"
  config
  echo "--- per suite"
  echo "$runner --reporters IDE --ide-reporter-show-killed --allow-surviving <suite>"
  exit 0
fi

for needed in "$llvm/bin/clang++" "$runner" "$plugin"; do
  [[ -e $needed ]] || { echo "mutation.sh: missing $needed (see CONTRIBUTING.md, Mutation testing)" >&2; exit 1; }
done

mkdir -p "$build"
if [[ ! -f $build/build.ninja ]]; then
  cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_CXX_COMPILER="$llvm/bin/clang++" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCE_WERROR=OFF -DCE_BUILD_TESTING=ON \
    "-DCMAKE_CXX_FLAGS=-O0 -g -grecord-command-line -fpass-plugin=$plugin"
fi
config > "$build/mull.yml"

if [[ -z $suites ]]; then
  suites=$(ctest --test-dir "$build" -N | sed -n 's/^ *Test *#[0-9]*: \(.*_test\)$/\1/p' | paste -sd, -)
fi
IFS=, read -r -a suite_list <<< "$suites"
cmake --build "$build" --target "${suite_list[@]}"

logs=()
cd "$build"
for suite in "${suite_list[@]}"; do
  log=$build/$suite.mull.log
  exe=$(find "$build" -type f -name "$suite" -perm -u+x | head -n 1)
  echo "== $suite" >&2
  "$runner" --reporters IDE --ide-reporter-show-killed --allow-surviving "$exe" > "$log" 2>&1 || true
  logs+=("$log")
done

python3 "$root/scripts/mutation_summary.py" "${logs[@]}"
