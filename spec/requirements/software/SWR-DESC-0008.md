---
uid: SWR-DESC-0008
title: A macro description keeps precedence under the reflection backend
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 requires that upgrading a project from C++20 to C++26 cannot silently change
  the serialized form; a type already carrying `CE_DESCRIBE` therefore keeps exactly the
  field set and order the macro declared, including its renames and omissions.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::macro-precedence-under-reflection]
owner: filip.sajdak
version: 1
---
Where a type carries a `CE_DESCRIBE` declaration and the C++26 backend is active, the
reflection seam shall report the field set, order and names declared by the macro rather
than those it would derive by reflection.
