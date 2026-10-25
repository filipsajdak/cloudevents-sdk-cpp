---
uid: SWR-PROTO-0001
title: protobuf_format encode and decode entry points
type: software
status: approved
priority: high
rationale: >
  The Protobuf format is bytes to event and event to bytes, the shape of json_format without a
  codec parameter, because the wire schema is fixed and the format needs no DOM. Working names;
  the API is an addition to ce::v4 (SWR-BUILD-0006).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_format_test.cpp::protobuf-format-entry-points]
owner: filip.sajdak
version: 1
---
The SDK shall provide `protobuf_format` with `encode` taking an event and returning a
`result<binary>`, and `decode` taking a `std::span<const std::byte>` and a
`protobuf::decode_options` defaulting to the limits of SWR-PROTO-0010 and returning a
`result<event>`, in `ce::v4` only, throwing no exception.
