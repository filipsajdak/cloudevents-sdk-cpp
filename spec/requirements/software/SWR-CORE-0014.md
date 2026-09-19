---
uid: SWR-CORE-0014
title: event aggregate carrying the required context attributes
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 and open decision D1 settle on a public aggregate gated by
  `validate()`, and CloudEvents v1.0.2 core specification section 3.1 names `id`,
  `source`, `specversion` and `type` as REQUIRED on every event.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-event-required-attributes]
owner: filip.sajdak
version: 1
---
The `event` type shall be an aggregate with public members `id`, `source`,
`specversion` defaulted to the value `1.0`, and `type`.
