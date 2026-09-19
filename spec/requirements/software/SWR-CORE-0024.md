---
uid: SWR-CORE-0024
title: constexpr is_json_content_type validator
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.1 and the CloudEvents JSON format specification section 3.1 decide
  payload handling from the media type, which RFC 2046 makes case-insensitive and
  allows to carry parameters, so the test covers both suffix and subtype forms.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-is-json-content-type]
owner: filip.sajdak
version: 1
---
The core header shall provide a `constexpr` predicate `is_json_content_type`
returning true for media types of the form `*/json` and `*/*+json` regardless of
letter case and regardless of trailing parameters.
