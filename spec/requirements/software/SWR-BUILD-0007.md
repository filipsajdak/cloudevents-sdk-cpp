---
uid: SWR-BUILD-0007
title: No deprecated or removed-in-C++26 library features
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 5 bars deprecated and removed-in-C++26 library facilities and makes
  deprecation warnings errors, so a future standard removing a facility cannot
  turn the upgrade into a port.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-no-deprecated-features]
owner: filip.sajdak
version: 1
---
The SDK shall use no standard library facility that is deprecated or removed in
C++26, with the build configured to treat deprecation warnings as errors.
