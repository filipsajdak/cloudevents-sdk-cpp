---
uid: SWR-PROTO-0009
title: Decode is strict and reports with the existing error codes
type: software
status: reviewed
priority: medium
rationale: >
  Neither SDK validates on decode (docs/spikes/protobuf.md, section 3) and the model of this SDK
  does, as its JSON decode does (ADR-0008). Each failure maps onto an errc enumerator that
  exists, so no enumerator is added and no exhaustive switch breaks.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf_format` decoder shall fail with `errc::parse_error` for truncated input, a length
beyond the remaining bytes, a varint of more than ten bytes or a wire type it cannot skip, with
`errc::missing_required_attribute` for an absent or empty id, source or type, with
`errc::unsupported_spec_version` for a spec_version other than 1.0, with `errc::invalid_utf8`
for a string that is not UTF-8, with `errc::out_of_range` for a ce_integer outside the range of
int32, with `errc::invalid_attribute_value` for an attribute value with no arm set, and with
`errc::type_mismatch` for a time, datacontenttype, dataschema or subject entry in the wrong arm;
it shall skip an unknown field and take the last of duplicate map keys.
