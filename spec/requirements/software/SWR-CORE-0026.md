---
uid: SWR-CORE-0026
title: Each context attribute is a type that cannot hold a forbidden value
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  CR-0001 found that validity was a property the caller was trusted to maintain
  rather than one the type guaranteed. ADR-0008 moves each rule into the type,
  which makes the check unavoidable instead of merely available, and removes the
  implicit conversions that let a string become either of two different
  CloudEvents types.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/attribute_types_test.cpp::attribute-types-refuse-invalid-text]
owner: filip.sajdak
version: 1
---
The core header shall define a distinct type for each of the `id`, `source`,
`type`, `subject`, `datacontenttype`, `dataschema` and extension-name attributes,
each constructible only through a factory returning a result or through a
compile-time checked literal.
