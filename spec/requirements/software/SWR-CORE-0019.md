---
uid: SWR-CORE-0019
title: A present optional string attribute refuses empty text at construction
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 forbids an empty string for
  any attribute that is present, so absence and emptiness must stay
  distinguishable. ADR-0008 makes the optional attribute types unable to hold an
  empty value, which leaves `std::nullopt` as the only way to say absent.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-validate-optional-non-empty]
owner: filip.sajdak
version: 2
---
When the text offered for a `datacontenttype`, `dataschema` or `subject`
attribute is empty, the factory constructing that attribute shall return a failed
result identifying the attribute.
