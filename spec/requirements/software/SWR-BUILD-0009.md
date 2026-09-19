---
uid: SWR-BUILD-0009
title: CE_DESCRIBE is the only macro leaking from the headers
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 7 permits a single macro to escape the headers because a macro
  crosses a module boundary only when exported deliberately, and every other
  macro would pollute the consumer translation unit.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-macro-leakage]
owner: filip.sajdak
version: 1
---
The installed headers shall leave `CE_DESCRIBE` as the only macro defined after
inclusion, undefining or scoping every other macro they introduce.
