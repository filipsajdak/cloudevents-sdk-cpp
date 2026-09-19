---
uid: SWR-CORE-0021
title: lint reports over-long extension names as warnings
type: software
status: approved
priority: medium
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 states that attribute names
  SHOULD be 20 characters or fewer, which is a recommendation rather than a
  constraint, so SPEC 5.1 keeps such names valid and surfaces them through a
  separate advisory operation.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-lint-long-extension-name]
owner: filip.sajdak
version: 1
---
When an extension attribute name is longer than 20 characters, `lint()` shall
report it as a warning while `validate()` accepts the event.
