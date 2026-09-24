---
uid: SWR-JSON-0019
title: data decodes to a json_document built by the decoding codec
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 preserves the received JSON without committing the SDK to interpreting it.
  CR-0003 keeps it as the document the decoder already parsed, so a typed read or a re-encode with the same codec needs no serialisation and no second parse.
  The decoder owns the document it parsed, so it moves the member out with the codec's extract rather than copying it (SWR-JSON-0039).
  A caller-supplied document passed to from_value stays the caller's, so from_value copies the member instead.
  SWR-JSON-0040 bounds the memory a retained document can pin.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::decode-data-yields-json-document]
owner: filip.sajdak
version: 2
---
When the json_format decoder reads a top-level data member and no rule selects another payload type, it shall place the member value into the event as a json_document built by the decoding codec, within the retention limit of SWR-JSON-0040.
