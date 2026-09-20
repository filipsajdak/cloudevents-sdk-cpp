#!/usr/bin/env bash
# Regenerate the interoperability goldens and verify both directions.
#
# Docker is the only requirement: the Go and Java toolchains stay in containers
# so the committed goldens remain usable without either installed (SWR-SEC-0005).
#
#   ./interop/run.sh            regenerate everything and verify
#   ./interop/run.sh verify     verify only, leaving the goldens untouched
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
fixtures="$root/test/fixtures/interop"
mode="${1:-regenerate}"

echo "==> building the C++ producer"
build="$root/build/interop"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCE_BUILD_TESTS=OFF >/dev/null
ctre_include="$(find "$build/_deps" -type d -path '*ctre-src/include' | head -1)"
: "${CXX:=c++}"
"$CXX" -std=c++20 -O1 \
  -I "$root/include" \
  ${ctre_include:+-isystem "$ctre_include"} \
  $(pkg-config --cflags nlohmann_json 2>/dev/null || echo "-isystem /usr/local/include -isystem /opt/homebrew/include") \
  "$root/interop/cpp/produce.cpp" -o "$build/produce"

if [ "$mode" != "verify" ]; then
  echo "==> producing the C++ documents"
  "$build/produce" "$fixtures/cpp"
fi

go_out="$fixtures/go"
java_out="$fixtures/java"
if [ "$mode" = "verify" ]; then
  go_out="$(mktemp -d)/go"
  java_out="$(mktemp -d)/java"
fi

echo "==> Go: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -w /w/interop/go -e GOFLAGS=-mod=mod golang:1.22 \
  sh -c "go mod tidy >/dev/null 2>&1; go run . '${go_out/#$root//w}' /w/test/fixtures/interop/cpp"

echo "==> Java: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -v "$HOME/.m2:/root/.m2" -w /w/interop/java \
  maven:3.9-eclipse-temurin-17 \
  mvn -q -B compile org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
    -Dexec.args="${java_out/#$root//w} /w/test/fixtures/interop/cpp"

echo
echo "both SDKs accepted every document this SDK produced"
