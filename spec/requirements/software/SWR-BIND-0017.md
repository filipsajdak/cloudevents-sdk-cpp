---
uid: SWR-BIND-0017
title: The copying entry points never modify their message
type: software
status: reviewed
priority: medium
rationale: >
  from_message and from_batch_message take a const message&, and a caller may pass std::move(m) to
  them and read m afterwards, which compiles and works today. Moving entry points are new names so
  that this keeps working, and this requirement pins it: a later overload of the same name would
  break the test and would need a rule 4 decision first (CR-0005).
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
When a caller passes a message to `from_message` of a binding, or to `http::from_batch_message`, as an lvalue, a const reference or the result of `std::move`, the binding shall leave the message unchanged.
