---
uid: SWR-BIND-0002
title: A fixed attribute emission order
type: software
status: approved
priority: high
rationale: >
  The interop and conformance fixtures compare whole messages, so the order fields are written in is observable. A reordering reads as tidying in review and fails in the fixtures, which is a long way from the change that caused it. Writing the order down makes it a contract rather than an accident of how the code happens to be arranged.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-emission-order]
owner: filip.sajdak
version: 1
---
The shared binding core shall emit specversion, id, source, type, dataschema,
subject, time and then the extensions in name order, and shall write the content
type last and without the attribute prefix.
