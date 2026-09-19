---
uid: SWR-CORE-0010
title: Lenient timestamp acceptance of lowercase separators and second 60
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 states that parsing accepts lowercase `t` and `z` and the leap second
  value 60, following RFC 3339 section 5.6 and the leniency principle of being
  tolerant on consume so events from other producers are not rejected.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-timestamp-lenient-parse]
owner: filip.sajdak
version: 1
---
When parsing RFC 3339 text, `timestamp::parse` shall accept a lowercase `t`
date-time separator, a lowercase `z` zone designator and a seconds field of 60.
