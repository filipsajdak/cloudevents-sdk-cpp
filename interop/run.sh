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

# Container-side paths only. Verify mode writes inside the repository rather
# than to a temporary directory, because only the repository is mounted; the
# scratch output is removed afterwards and never committed.
if [ "$mode" = "verify" ]; then
  go_out=/w/build/interop-verify/go
  java_out=/w/build/interop-verify/java
  mkdir -p "$root/build/interop-verify/go" "$root/build/interop-verify/java"
else
  go_out=/w/test/fixtures/interop/go
  java_out=/w/test/fixtures/interop/java
fi

echo "==> Go: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -e GOFLAGS=-mod=mod golang:1.22 \
  sh -c "cd /w/interop/go && go mod tidy >/dev/null 2>&1; \
         cd /w/interop/go && go run . '$go_out' /w/test/fixtures/interop/cpp"

echo "==> Java: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -v "$HOME/.m2:/root/.m2" \
  maven:3.9-eclipse-temurin-17 \
  sh -c "cd /w/interop/java && mvn -q -B compile \
         org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
         -Dexec.args='$java_out /w/test/fixtures/interop/cpp'"

if [ "$mode" = "verify" ]; then
  rm -rf "$root/build/interop-verify"
fi

echo
echo "both SDKs accepted every document this SDK produced"
