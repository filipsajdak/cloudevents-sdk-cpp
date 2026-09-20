#!/usr/bin/env bash
# Compile time and binary size per codec.
#
# Both matter for a header-only SDK: the codec is instantiated in every
# translation unit that touches the format layer, so its compile cost is paid
# repeatedly and its code size lands in the binary.
#
# Compile time is wall clock and therefore sensitive to what else the machine is
# doing. Each codec is built three times and the FASTEST is reported, which is
# the least contaminated sample rather than an average of contaminated ones.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
ctre="$(find "$root/build" -type d -path '*ctre-src/include' 2>/dev/null | head -1)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

: "${CXX:=clang++}"
flags=(-std=c++23 -O2 -DNDEBUG -I "$root/include" -I "$root/bench"
       -isystem "$ctre" -isystem /opt/homebrew/include)

printf '%-12s %10s %12s %12s\n' codec 'compile s' 'binary KiB' 'stripped KiB'

i=0
for name in nlohmann rapidjson boost.json glaze; do
  i=$((i + 1))
  extra=()
  sources=("$root/bench/one_codec.cpp")
  if [ "$name" = "boost.json" ]; then
    sources+=("$root/bench/boost_json_src.cpp")
  fi

  best=""
  for _ in 1 2 3; do
    start=$(python3 -c 'import time; print(time.monotonic())')
    "$CXX" "${flags[@]}" -DCE_BENCH_CODEC=$i "${sources[@]}" \
      "${extra[@]}" -o "$out/$name" 2>/dev/null
    end=$(python3 -c 'import time; print(time.monotonic())')
    elapsed=$(python3 -c "print(f'{$end - $start:.2f}')")
    if [ -z "$best" ] || python3 -c "import sys; sys.exit(0 if $elapsed < $best else 1)"; then
      best=$elapsed
    fi
  done

  size=$(( $(stat -f%z "$out/$name") / 1024 ))
  strip -x "$out/$name" 2>/dev/null || true
  stripped=$(( $(stat -f%z "$out/$name") / 1024 ))
  printf '%-12s %10s %12s %12s\n' "$name" "$best" "$size" "$stripped"
done
