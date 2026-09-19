---
uid: SWR-DESC-0007
title: Reflection backend describes public members only
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 limits the C++26 backend to public members so that the field set of a type is
  the same one a caller could write by hand with the macro, keeping the two backends in
  parity and keeping private state out of the serialized form.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::reflection-backend-public-only]
owner: filip.sajdak
version: 1
---
The C++26 backend shall describe only the public non-static data members of a type,
excluding its protected and private data members.
