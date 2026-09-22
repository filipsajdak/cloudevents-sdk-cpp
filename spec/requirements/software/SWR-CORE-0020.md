---
uid: SWR-CORE-0020
title: The extension name grammar is enforced by the extension_name type
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 restricts attribute names to
  lowercase letters and digits, and SPEC 5.1 additionally bars names that collide
  with a context attribute defined by the specification. ADR-0008 folds both rules
  into the type used as the map key, so an extension map cannot hold an entry the
  encoder would later refuse.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-extension-names-refuse-invalid]
owner: filip.sajdak
version: 2
---
When the text offered for an extension name fails to match the pattern
`[a-z0-9]+` or names a reserved context attribute, the `extension_name` factory
shall return a failed result naming the offending text.
