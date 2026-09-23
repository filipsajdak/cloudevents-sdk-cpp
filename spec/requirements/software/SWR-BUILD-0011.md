---
uid: SWR-BUILD-0011
title: ce::v1 keeps the surface v0.3.0 published
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  SPEC 3 rule 4 and SWR-BUILD-0006 require that a breaking change leave the
  published generation intact. v0.4.0 broke most of what v0.3.0 published
  (CR-0002), so ce::v1 carries the v0.3.0 declarations forward. The v0.3.0 suites
  are the evidence: they are what v0.3.0 was released against, and running them
  unchanged against ce::v1 is what shows it kept its behaviour as well as its
  declarations.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/v1/core.hpp, code:include/cloudevents/v1/detail/timestamp.hpp, code:include/cloudevents/v1/binding/common.hpp]
verified_by: [test:test/v1/CMakeLists.txt::ce-v1-suites, test:test/v1/v1_fixes_test.cpp::v1-defect-fixes]
owner: filip.sajdak
version: 1
---
The SDK shall declare, in the non-inline namespace `ce::v1`, every public entity that
v0.3.0 published, with its v0.3.0 declaration, shall apply to `ce::v1` every defect fix
that keeps those declarations, and shall run the v0.3.0 suites against `ce::v1`.
