---
uid: SWR-DESC-0006
title: C++26 backend built on static reflection with annotations
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 pins the C++26 backend to `nonstatic_data_members_of` with an explicit access
  context and to `identifier_of` for names, so field discovery needs no macro, while
  annotations keep the rename and skip controls the macro form offers.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::reflection-backend-describe]
owner: filip.sajdak
version: 1
---
The C++26 backend shall enumerate fields with `nonstatic_data_members_of` under an
explicit access context, take each name from `identifier_of`, and honour the
`[[=ce::name("x")]]` and `[[=ce::skip]]` annotations for renaming and omitting a member.
