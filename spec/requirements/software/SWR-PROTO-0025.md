---
uid: SWR-PROTO-0025
title: JSON-typed data can be classified on decode
type: software
status: approved
priority: medium
rationale: >
  Ruled by the owner on 2026-10-25 (CR-0009, item 8). The default decode returns text_data as a
  `std::string` and binary_data as a `binary`, as the wire says, which is SWR-PROTO-0008. A caller that wants
  JSON gets it from an optional, separately named entry point, so no existing call changes. The owner ruled
  on 2026-10-25 (item 21) that there is no retention: the payload stays text or bytes in the event, decode_as
  parses it on demand, and protobuf::decode_options carries only the decode limits.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_batch_test.cpp::protobuf-decode-as-classifies-json, test:test/protobuf_batch_test.cpp::protobuf-default-decode-keeps-text]
owner: filip.sajdak
version: 1
---
The `protobuf_format` class shall provide `decode_as<Codec>`, which shall decode as `decode` does, with the same `protobuf::decode_options`, and, when the datacontenttype of the event is a JSON content type, also parse
its text_data or binary_data payload with `Codec` and return the value, holding no document in the event; the payload of the event
stays as `decode` returns it.
