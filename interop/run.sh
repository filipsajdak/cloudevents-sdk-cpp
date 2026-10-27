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

"$CXX" -std=c++20 -O1 \
  -I "$root/include" \
  ${ctre_include:+-isystem "$ctre_include"} \
  $(pkg-config --cflags nlohmann_json 2>/dev/null || echo "-isystem /usr/local/include -isystem /opt/homebrew/include") \
  "$root/interop/kafka/cpp/produce.cpp" -o "$build/produce-kafka"

"$CXX" -std=c++20 -O1 \
  -I "$root/include" \
  ${ctre_include:+-isystem "$ctre_include"} \
  $(pkg-config --cflags nlohmann_json 2>/dev/null || echo "-isystem /usr/local/include -isystem /opt/homebrew/include") \
  "$root/interop/mqtt/cpp/produce.cpp" -o "$build/produce-mqtt"

# The Protobuf format has no dependency beyond the core, so no JSON library is on this line.
"$CXX" -std=c++20 -O1 \
  -I "$root/include" \
  ${ctre_include:+-isystem "$ctre_include"} \
  "$root/interop/protobuf/cpp/produce.cpp" -o "$build/produce-protobuf"

if [ "$mode" != "verify" ]; then
  echo "==> producing the C++ documents"
  "$build/produce" "$fixtures/cpp"
fi

# The Kafka records are always produced afresh, even in verify mode: both SDKs
# must read what the library writes today, and the committed copy must be what
# it writes today (checked below), or the cpp column would test stale bytes.
if [ "$mode" = "verify" ]; then
  kafka_cpp="$root/build/interop-verify/kafka-cpp"
  rm -rf "$kafka_cpp"
  mkdir -p "$kafka_cpp"
  kafka_cpp_in_container=/w/build/interop-verify/kafka-cpp
else
  kafka_cpp="$fixtures/kafka/cpp"
  kafka_cpp_in_container=/w/test/fixtures/interop/kafka/cpp
fi
echo "==> producing the C++ Kafka records"
"$build/produce-kafka" "$kafka_cpp"

# The MQTT messages are produced afresh in the same way, for the same reason.
if [ "$mode" = "verify" ]; then
  mqtt_cpp="$root/build/interop-verify/mqtt-cpp"
  rm -rf "$mqtt_cpp"
  mkdir -p "$mqtt_cpp"
  mqtt_cpp_in_container=/w/build/interop-verify/mqtt-cpp
else
  mqtt_cpp="$fixtures/mqtt/cpp"
  mqtt_cpp_in_container=/w/test/fixtures/interop/mqtt/cpp
fi
echo "==> producing the C++ MQTT messages"
"$build/produce-mqtt" "$mqtt_cpp"
# Like the Kafka records, the Protobuf bytes are produced afresh in verify mode: both SDKs must
# decode what the library writes today, and the committed copy must be what it writes today.
if [ "$mode" = "verify" ]; then
  protobuf_cpp="$root/build/interop-verify/protobuf-cpp"
  rm -rf "$protobuf_cpp"
  mkdir -p "$protobuf_cpp"
  protobuf_cpp_in_container=/w/build/interop-verify/protobuf-cpp
else
  protobuf_cpp="$fixtures/protobuf/cpp"
  protobuf_cpp_in_container=/w/test/fixtures/interop/protobuf/cpp
fi
echo "==> producing the C++ Protobuf events and messages"
"$build/produce-protobuf" "$protobuf_cpp"

# Container-side paths only. Verify mode writes inside the repository rather
# than to a temporary directory, because only the repository is mounted; the
# scratch output is removed afterwards and never committed.
if [ "$mode" = "verify" ]; then
  go_out=/w/build/interop-verify/go
  java_out=/w/build/interop-verify/java
  kafka_go_out=/w/build/interop-verify/kafka-go
  kafka_java_out=/w/build/interop-verify/kafka-java
  mqtt_go_out=/w/build/interop-verify/mqtt-go
  protobuf_go_out=/w/build/interop-verify/protobuf-go
  protobuf_java_out=/w/build/interop-verify/protobuf-java
  mkdir -p "$root/build/interop-verify/go" "$root/build/interop-verify/java" \
    "$root/build/interop-verify/kafka-go" "$root/build/interop-verify/kafka-java" \
    "$root/build/interop-verify/mqtt-go" \
    "$root/build/interop-verify/protobuf-go" "$root/build/interop-verify/protobuf-java"
else
  go_out=/w/test/fixtures/interop/go
  java_out=/w/test/fixtures/interop/java
  kafka_go_out=/w/test/fixtures/interop/kafka/go
  kafka_java_out=/w/test/fixtures/interop/kafka/java
  mqtt_go_out=/w/test/fixtures/interop/mqtt/go
  protobuf_go_out=/w/test/fixtures/interop/protobuf/go
  protobuf_java_out=/w/test/fixtures/interop/protobuf/java
fi

echo "==> Go: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -w /w/interop/go -e GOFLAGS=-mod=mod golang:1.22 \
  sh -ec 'cd /w/interop/go
          test -f go.mod || { echo "go.mod is not where run.sh expects it"; ls -la; exit 1; }
          go mod tidy >/dev/null 2>&1 || true
          exec go run . "$1" /w/test/fixtures/interop/cpp' sh "$go_out"

echo "==> Java: generating goldens and reading the C++ documents"
docker run --rm -v "$root:/w" -v "$HOME/.m2:/root/.m2" -w /w/interop/java \
  maven:3.9-eclipse-temurin-17 \
  sh -ec 'cd /w/interop/java
          exec mvn -q -B compile org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
               -Dexec.args="$1 /w/test/fixtures/interop/cpp"' sh "$java_out"

# The Kafka bindings of both SDKs convert to and from an in-memory record, so no
# broker is started. Each program writes its own records and then decodes the
# C++ ones, failing on the first record that does not decode to its event.
echo "==> Go: Kafka records, written and read without a broker"
docker run --rm -v "$root:/w" -w /w/interop/kafka/go -e GOFLAGS=-mod=mod golang:1.22 \
  sh -ec 'cd /w/interop/kafka/go
          test -f go.mod || { echo "go.mod is not where run.sh expects it"; ls -la; exit 1; }
          exec go run . "$1" "$2"' sh "$kafka_go_out" "$kafka_cpp_in_container"

echo "==> Java: Kafka records, written and read without a broker"
docker run --rm -v "$root:/w" -v "$HOME/.m2:/root/.m2" -w /w/interop/kafka/java \
  maven:3.9-eclipse-temurin-17 \
  sh -ec 'cd /w/interop/kafka/java
          exec mvn -q -B compile org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
               -Dexec.args="$1 $2"' sh "$kafka_java_out" "$kafka_cpp_in_container"

# The MQTT binding of sdk-go converts an event to and from an in-memory paho.Publish,
# which is the topic, the payload and the properties a broker would carry, so no
# broker is started. sdk-java has no MQTT binding, so Go is the only peer.
echo "==> Go: MQTT messages, written and read without a broker"
docker run --rm -v "$root:/w" -w /w/interop/mqtt/go -e GOFLAGS=-mod=mod golang:1.26 \
  sh -ec 'cd /w/interop/mqtt/go
          test -f go.mod || { echo "go.mod is not where run.sh expects it"; ls -la; exit 1; }
          exec go run . "$1" "$2"' sh "$mqtt_go_out" "$mqtt_cpp_in_container"
# The Protobuf format needs no broker either: the bytes are the whole interface. Each program
# writes its own bytes, and the structured message of its HTTP and Kafka bindings for each (the
# headers and the body, in memory, SWR-PROTO-0023), and then decodes the C++ ones, failing on the
# first event that does not decode to the corpus event. The protoc cases are not regenerated here; see
# interop/protobuf/protoc/gen_edge.py, which needs protoc and the v1.0.2 cloudevents.proto.
echo "==> Go: Protobuf bytes and HTTP and Kafka messages, written and read"
docker run --rm -v "$root:/w" -w /w/interop/protobuf/go -e GOFLAGS=-mod=mod golang:1.22 \
  sh -ec 'cd /w/interop/protobuf/go
          test -f go.mod || { echo "go.mod is not where run.sh expects it"; ls -la; exit 1; }
          go run . gen "$1"
          exec go run . verify "$2"' sh "$protobuf_go_out" "$protobuf_cpp_in_container"

echo "==> Java: Protobuf bytes and HTTP and Kafka messages, written and read"
docker run --rm -v "$root:/w" -v "$HOME/.m2:/root/.m2" -w /w/interop/protobuf/java \
  maven:3.9-eclipse-temurin-17 \
  sh -ec 'cd /w/interop/protobuf/java
          mvn -q -B compile org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
              -Dexec.args="gen $1"
          exec mvn -q -B compile org.codehaus.mojo:exec-maven-plugin:3.1.0:java \
               -Dexec.args="verify $2"' sh "$protobuf_java_out" "$protobuf_cpp_in_container"

if [ "$mode" = "verify" ]; then
  # What each side writes today must be the committed bytes, file for file.
  for side in go java cpp; do
    produced="$root/build/interop-verify/protobuf-$side"
    diff -r "$fixtures/protobuf/$side" "$produced" || {
      echo "the committed Protobuf bytes for $side are not what the producer writes now" >&2
      exit 1
    }
  done
fi

if [ "$mode" = "verify" ]; then
  # What each side writes today must be the committed record, byte for byte.
  for side in go cpp; do
    produced="$root/build/interop-verify/mqtt-$side"
    diff -r "$fixtures/mqtt/$side" "$produced" || {
      echo "the committed MQTT messages for $side are not what the producer writes now" >&2
      exit 1
    }
  done
  for side in go java cpp; do
    produced="$root/build/interop-verify/kafka-$side"
    diff -r "$fixtures/kafka/$side" "$produced" || {
      echo "the committed Kafka records for $side are not what the producer writes now" >&2
      exit 1
    }
  done
fi

if [ "$mode" = "verify" ]; then
  # Removed from inside a container, because the generators run as root and
  # create subdirectories the host user cannot empty: unlinking a root-owned
  # file needs write permission on its DIRECTORY, which a root-owned
  # subdirectory does not grant.
  docker run --rm -v "$root:/w" alpine:3 rm -rf /w/build/interop-verify
fi

echo
echo "both SDKs accepted every document and every Kafka record this SDK produced"
echo "and Go accepted every MQTT message this SDK produced"
echo "both SDKs accepted every document, every Kafka record and every Protobuf event this SDK produced,"
echo "and every Protobuf message of the HTTP and Kafka bindings"
