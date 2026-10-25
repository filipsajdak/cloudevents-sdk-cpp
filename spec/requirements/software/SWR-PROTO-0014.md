---
uid: SWR-PROTO-0014
title: A header-only ce::format_protobuf target, installed and packaged
type: software
status: approved
priority: medium
rationale: >
  A consumer takes the format by linking one target, as it takes ce::format_json, and the
  recipes of the two package managers list the new header so that an installed copy has it.
  Because the format has no dependency, the target is defined whenever the core is, and needs no
  switch.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:CMakeLists.txt, code:packaging/vcpkg/ports/cloudevents-cpp/usage, code:packaging/conan/conanfile.py]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-protobuf-roundtrip, test:test/consumer/CMakeLists.txt::consumer-target-graph-protobuf]
owner: filip.sajdak
version: 1
---
The SDK shall define the header-only interface target `ce::format_protobuf`, linking `ce::core`
only, install it with the package configuration, list it in the vcpkg overlay port and the Conan
recipe, and prove by `test/consumer` that an installed copy builds and round-trips an event
through it.
