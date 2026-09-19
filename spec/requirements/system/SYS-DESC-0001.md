---
uid: SYS-DESC-0001
title: One reflection seam serving typed payloads and typed extensions
type: system
status: approved
priority: high
rationale: >
  Mapping a user struct to JSON members, to extension attributes and to headers is
  the same problem three times. Solving it once behind a customization point means
  the C++26 static reflection backend can replace the C++20 macro without any
  consumer changing a call site.
verification_method: test
security_classification: operational
derived_from: [STK-EVOLVE-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::describe-backend-parity]
owner: filip.sajdak
version: 1
---
The SDK shall expose one field-enumeration interface over user-defined structs whose
observable behaviour is identical whether it is served by the C++20 macro backend or
the C++26 static reflection backend.
