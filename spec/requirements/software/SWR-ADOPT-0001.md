---
uid: SWR-ADOPT-0001
title: Four header-only targets with a downward dependency direction
type: software
status: approved
priority: high
rationale: >
  SPEC 4 fixes the target graph so that a consumer can take the core without the JSON
  codec. Enforcing the direction in the build, rather than by convention, is what
  keeps the core free of third-party headers.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:CMakeLists.txt]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-target-graph]
owner: filip.sajdak
version: 1
---
The SDK shall publish the core, JSON format, nlohmann codec and HTTP binding as
header-only interface targets whose includes run from the bindings and formats
towards the core and never in the reverse direction.
