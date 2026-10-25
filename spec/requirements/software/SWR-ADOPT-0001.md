---
uid: SWR-ADOPT-0001
title: Header-only targets with a downward dependency direction
type: software
status: approved
priority: high
rationale: >
  SPEC 4 fixes the target graph so that a consumer can take the core without the JSON
  codec. Enforcing the direction in the build, rather than by convention, is what
  keeps the core free of third-party headers. The Protobuf format is a fifth target (CR-0009): it is hand-written, depends on the core alone and so needs no component or switch.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:CMakeLists.txt]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-target-graph, test:test/consumer/CMakeLists.txt::consumer-target-graph-protobuf]
owner: filip.sajdak
version: 2
---
The SDK shall publish the core, JSON format, Protobuf format, nlohmann codec and HTTP
binding as header-only interface targets whose includes run from the bindings and formats
towards the core and never in the reverse direction.
