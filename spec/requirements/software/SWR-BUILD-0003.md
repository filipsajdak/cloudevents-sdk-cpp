---
uid: SWR-BUILD-0003
title: Capabilities exposed as CE_HAS constants
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 1 requires each detected capability to surface as a `CE_HAS_*`
  constant so consuming code branches on a named value that participates in
  ordinary compilation rather than on preprocessor state.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/detail/config.hpp]
verified_by: [test:test/config_test.cpp::config-ce-has-constants]
owner: filip.sajdak
version: 1
---
The configuration header shall expose each detected capability as a `constexpr
bool` constant named with the prefix `CE_HAS_`.
