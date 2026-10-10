---
uid: SWR-EXT-0013
title: set_data refuses a datacontenttype that is not JSON
type: software
status: approved
priority: medium
rationale: >
  set_data stores a JSON payload, so an event whose datacontenttype says text/plain would describe it wrongly.
  In ce::v3 set_data overwrote the caller's media type with application/json (SWR-EXT-0011, v0.5.0), which silently discards an attribute the caller set.
  encode_as already refuses such an event with type_mismatch rather than rewrite it (SWR-EXT-0010, D-JSON-9), and the owner decided on 2026-09-28 that ce::v4 set_data follows the same rule.
  A JSON media type is one is_json_content_type accepts (SWR-CORE-0024), which is the test encode_as applies.
  set_data and event_of::set_data return result<void>, and event_of::with_data returns result<event_of>, as the owner confirmed on 2026-09-29, so the refusal can be reported; the event is left as it was so a refused call has no effect to undo.
  The owner confirmed on 2026-09-29 that the predicate as shipped is the rule, so text/json is kept as well; D-JSON-9 states the same for encode_as.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/typed_payload_test.cpp::set-data-refuses-a-non-json-media-type]
owner: filip.sajdak
version: 3
---
If the event declares a `datacontenttype` that `is_json_content_type` does not accept, then `set_data<T, Codec>` shall return a failed `result` with `errc::type_mismatch` naming `datacontenttype`, and leave the event unchanged.
