---
uid: SWR-DESC-0005
title: Macro rename form decoupling wire names from member names
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.2 provides `CE_FIELD` because a wire name such as `data_base64` or a name that
  is a C++ keyword cannot always be spelled as a member identifier, and the struct must
  stay idiomatic while the serialized form stays fixed.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/detail/describe_macro.hpp]
verified_by: [test:test/describe_parity_test.cpp::macro-backend-rename]
owner: filip.sajdak
version: 1
---
The C++20 backend shall accept a `CE_FIELD(member, "wire_name")` entry in a
`CE_DESCRIBE` list and report the given wire name in place of the member identifier.
