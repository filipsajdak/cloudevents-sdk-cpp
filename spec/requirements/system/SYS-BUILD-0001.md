---
uid: SYS-BUILD-0001
title: Language-version gating confined to one header
type: system
status: approved
priority: high
rationale: >
  Scattered version checks are what makes a library expensive to move to a new
  standard. Confining every conditional to one header, and keying it on feature-test
  macros rather than standard version, makes the upgrade a deletion in one file.
verification_method: test
security_classification: operational
derived_from: [STK-EVOLVE-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-single-gate]
owner: filip.sajdak
version: 1
---
The SDK shall confine every conditional compilation directive that selects a language
or library feature to one configuration header, and shall key each such directive on
a feature-test macro.
