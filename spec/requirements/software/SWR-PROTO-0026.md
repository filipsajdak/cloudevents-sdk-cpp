---
uid: SWR-PROTO-0026
title: The proto_data type URL has a side channel
type: software
status: reviewed
priority: medium
rationale: >
  Ruled by the owner on 2026-10-25 (CR-0009, items 9 and 13). A proto_data carries a type URL that
  the SDK's event cannot hold apart from dataschema, and a data_t alternative for it would be a break
  (CR-0009, rule 4 verdict d). The side channel returns and accepts the URL beside the event. The
  owner ruled on 2026-10-25 (item 22): an empty URL counts as absent, a non-empty URL that is not a valid
  URI-reference is parse_error, and on encode the dataschema attribute is written even when it differs from the URL.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_data_test.cpp::protobuf-data-read-arms, test:test/protobuf_batch_test.cpp::protobuf-proto-data-write-rule, test:test/protobuf_batch_test.cpp::protobuf-proto-data-read-rule, test:test/protobuf_batch_test.cpp::protobuf-proto-data-type-url-side-channel]
owner: filip.sajdak
version: 1
---
The `protobuf_format` class shall provide `decode_detailed`, returning an event together with the type URL
of its proto_data value, and `encode_proto_data`, taking an event and a type URL and writing the payload to
proto_data with that URL and the dataschema of the event, if any, as its own attribute even when it differs from the URL; an empty type URL
shall count as absent, and a non-empty one that is not a valid URI-reference shall fail with `errc::parse_error`.
