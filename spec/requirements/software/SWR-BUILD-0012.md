---
uid: SWR-BUILD-0012
title: The v0.5.0 API is ce::v3, and ce::v2 keeps what v0.4.0 published
type: software
status: implemented
delivered_in: v0.5.0
priority: high
rationale: >
  CR-0003 adds an alternative to data_t, which v0.4.0 published in ce::v2.
  SWR-BUILD-0006 and SPEC section 3 rule 4 put a breaking change in a new namespace and never change the old one.
  ADR-0010 applies the layout ADR-0009 set for v1.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/v2/core.hpp]
verified_by: [test:test/v2/CMakeLists.txt::ce-v2-suites, test:test/v2/v2_generation_test.cpp::v2-declarations-survive]
owner: filip.sajdak
version: 1
---
The SDK shall declare its API in `ce::inline v3` and keep every entity that v0.4.0 published reachable as `ce::v2::X` with its v0.4.0 declaration.
