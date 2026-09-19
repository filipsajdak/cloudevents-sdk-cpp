---
uid: SWR-BUILD-0002
title: All capability gating confined to detail/config.hpp
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 1 and SPEC 10 place every capability test in one header, so raising
  the language floor is a single-file edit and no other header changes meaning
  with the standard in use.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-gating-single-header]
owner: filip.sajdak
version: 1
---
The header `include/cloudevents/detail/config.hpp` shall be the only header
containing a feature-test preprocessor conditional, apart from the describe
backend headers.
