---
uid: SWR-BUILD-0001
title: Standard detection through feature-test macros only
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 1 forbids deducing capabilities from compiler or standard version
  numbers, because a feature-test macro states what the toolchain actually
  provides and keeps the C++29 upgrade a recompile rather than a port.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/detail/config.hpp]
verified_by: [test:test/config_test.cpp::config-feature-test-macros-only]
owner: filip.sajdak
version: 1
---
The SDK shall detect library and language capabilities from feature-test macros
alone, without testing a compiler identifier, a compiler version or the value of
`__cplusplus`.
