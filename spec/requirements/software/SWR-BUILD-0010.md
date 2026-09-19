---
uid: SWR-BUILD-0010
title: Optional cloudevents.cppm module wrapper outside the default build
type: software
status: approved
priority: medium
rationale: >
  SPEC 3 rule 7 ships a module interface that re-exports the headers, and keeps
  it out of the default build because module support across the toolchains named
  in SPEC 8 is uneven and would otherwise break the floor configuration.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-module-wrapper-optional]
owner: filip.sajdak
version: 1
---
The SDK shall ship a `cloudevents.cppm` module interface that exports the public
headers and is excluded from the default CMake build.
