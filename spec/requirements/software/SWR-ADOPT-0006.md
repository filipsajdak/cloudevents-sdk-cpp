---
uid: SWR-ADOPT-0006
title: The repository ships a vcpkg overlay port that a consumer builds against in CI
type: software
status: approved
priority: medium
rationale: >
  A project that manages its dependencies with vcpkg cannot adopt the SDK through find_package alone: it would have to install the SDK by hand and keep CTRE and the codec library in step with it.
  SPEC section 9 deferred the vcpkg port (D7); the owner decided on 2026-10-21 to ship the recipe inside this repository and to leave submission to the public registry as a separate, later decision (D-ADOPT-2).
  A port nobody builds rots, so CI installs it and compiles test/consumer against the result with the system copies of the dependencies hidden, which is the arrangement that exposes a missing dependency (D-ADOPT-1).
  CTRE and the codec libraries come from vcpkg, and the port passes the CMake switches that make a missed lookup a configure error, because the project falls back to downloading CTRE and nlohmann when find_package misses, and a build that quietly downloaded would prove nothing about the port.
  The nlohmann codec is a default feature, matching CE_CODECS; rapidjson and boost-json are features, and a build without any codec is the port with default features off.
  The boost-json feature depends on a compiled Boost.JSON, so CI does not build it; the port declares it and says so.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:packaging/vcpkg/ports/cloudevents-cpp/portfile.cmake, code:packaging/vcpkg/ports/cloudevents-cpp/vcpkg.json, code:.github/workflows/packaging.yml]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-roundtrip, test:test/consumer/CMakeLists.txt::consumer-codec-link]
owner: filip.sajdak
version: 1
---
The repository shall ship the overlay port `packaging/vcpkg/ports/cloudevents-cpp`, which installs the headers, the CMake package configuration and one vcpkg feature per codec that requires only that codec's dependency, and a CI job shall install it and build and run `test/consumer` against the installed result using only what vcpkg installed.
