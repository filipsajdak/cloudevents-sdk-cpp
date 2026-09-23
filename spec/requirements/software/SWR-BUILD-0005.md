---
uid: SWR-BUILD-0005
title: Public API published through the inline namespace of the current generation
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  SPEC 3 rule 4 places the public API in an inline versioned namespace so the
  mangled names carry the API generation and a consumer linking two generations
  gets a link error instead of silent one-definition-rule breakage. v0.4.0 is the
  second generation (CR-0002), so the inline namespace is `ce::v2`; an entity that
  did not change stays declared in `ce::v1` and is brought into `ce::v2`, so both
  spellings name one type.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/core.hpp, code:include/cloudevents/attributes.hpp, code:include/cloudevents/result.hpp]
verified_by: [test:test/build_test.cpp::config-inline-namespace-v2]
owner: filip.sajdak
version: 2
---
The SDK shall declare every public entity of the current API generation inside the inline
namespace `ce::v2`, or bring it into `ce::v2` with a using-declaration where the entity is
unchanged from `ce::v1`.
