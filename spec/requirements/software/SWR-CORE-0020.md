---
uid: SWR-CORE-0020
title: validate enforces the extension attribute name grammar
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 restricts attribute names to
  lowercase letters and digits, and SPEC 5.1 additionally bars names that collide
  with a context attribute defined by the specification.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-validate-extension-names]
owner: filip.sajdak
version: 1
---
When an extension attribute name does not match the pattern `[a-z0-9]+` or names
a reserved context attribute, `validate()` shall return a failed result naming
that extension.
