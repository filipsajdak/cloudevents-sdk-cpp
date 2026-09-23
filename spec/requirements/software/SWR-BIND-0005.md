---
uid: SWR-BIND-0005
title: A prefixed datacontenttype is refused where the binding has a content-type field
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  Where the media type travels in the binding's own content-type field, a prefixed
  `datacontenttype` matched no attribute branch and reached the extension branch,
  where the name passed the grammar check and was stored. `validate()` then
  refused it as a reserved name, which is true and useless: the name is not the
  problem, the field has no place in this binding, and nothing in that diagnosis
  said where the media type belongs.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-round-trip]
owner: filip.sajdak
version: 1
---
Where a binding carries the media type in its own content-type field, reading a
prefixed `datacontenttype` field shall return a failed result naming the
attribute and the field the media type belongs in.
