---
uid: SWR-BUILD-0009
title: CE_DESCRIBE is the only public macro the headers leave defined
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 7 permits a single public macro because every macro a header leaves
  defined pollutes the consumer translation unit. Three kinds of name cannot be
  undefined and are reserved instead (D-CONFIG-1): the `CE_HAS_*` capability
  macros, which gate an include and parsing no constant can gate; `CE_FIELD`,
  which SWR-DESC-0005 makes part of `CE_DESCRIBE`'s argument syntax; and the
  `CE_DETAIL_*` helpers, which `CE_DESCRIBE` expands into at the use site. A user
  must not define a reserved name, nor use one except `CE_FIELD` inside a
  `CE_DESCRIBE` list.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-macro-leakage]
owner: filip.sajdak
version: 2
---
When a translation unit includes the public headers, the headers shall leave no
macro defined other than the public macro `CE_DESCRIBE` and the reserved macros
`CE_FIELD`, `CE_HAS_EXPECTED`, `CE_HAS_REFLECTION`, `CE_HAS_EXPANSION_STATEMENTS`,
`CE_HAS_EXCEPTIONS` and those whose names begin with `CE_DETAIL_`.
