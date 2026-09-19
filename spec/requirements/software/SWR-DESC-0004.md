---
uid: SWR-DESC-0004
title: C++20 macro backend describing up to thirty-two members
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 makes the macro the backend for the C++20 toolchain floor, generating
  name and member-pointer pairs at namespace scope; the thirty-two member ceiling is the
  stated bound on the generated overload set.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::macro-backend-describe]
owner: filip.sajdak
version: 1
---
The C++20 backend shall provide a `CE_DESCRIBE(Type, member...)` macro, usable at
namespace scope, that generates name and member-pointer pairs for up to thirty-two
listed members.
