---
uid: SWR-ADOPT-0005
title: The installed package does not require the optional codec's dependency
type: software
status: approved
priority: high
rationale: >
  SWR-ADOPT-0002 states that the core depends on no third-party library except CTRE. A package configuration that calls find_dependency for nlohmann contradicts that at consume time: a consumer wanting only ce::core cannot use the package at all unless nlohmann is installed. The defect is invisible wherever nlohmann happens to be present, which is every continuous integration runner that installs it or stages a fetched copy.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:cmake/cloudeventsConfig.cmake.in]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-no-nlohmann]
owner: filip.sajdak
version: 1
---
When nlohmann_json is not available, `find_package(cloudevents)` shall succeed and
shall provide the core, JSON format and HTTP binding targets, reporting the
`codec_nlohmann` component as not found. When that component is requested
explicitly and nlohmann_json is not available, the configuration shall fail with a
message naming the missing dependency.
