---
uid: SWR-JSON-0045
title: json_format declares no public attribute-reading helpers
type: software
status: implemented
delivered_in: v0.6.0
priority: low
rationale: >
  The ce::v3 json_format declares seven public static helpers: required_text, optional_text, read_required, read_optional, read_time, read_context_attributes and read_extensions.
  Nothing in the tree calls them, no suite tests them and no requirement names them; they wrap private readers that decode uses directly.
  A generation may add to its API after its first tag but never remove from it (docs/RELEASE.md), so v0.6.0 is the only moment ce::v4 can be published without them.
  The owner decided on 2026-09-28 that ce::v4 does not declare them.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::json-format-declares-no-attribute-helpers, test:test/v3/v3_generation_test.cpp::v3-declarations-survive]
owner: filip.sajdak
version: 2
---
The `json_format<Codec>` class template shall not declare members named `required_text`, `optional_text`, `read_required`, `read_optional`, `read_time`, `read_context_attributes` or `read_extensions`.
