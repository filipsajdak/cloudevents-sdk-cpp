---
uid: SWR-PROTO-0019
title: http structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  An HTTP message in structured mode carries the whole event in the body with the content type
  of the format. The owner ruled on 2026-10-25 (CR-0009, items 11 and 12) that the entry points are new names
  and that the published from_message does not detect the format, and on 2026-10-27 (items 23 and 25) the exact
  names and what a message of another content type returns.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: [code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/detail/protobuf_structured.hpp]
verified_by: [test:test/http_binding_test.cpp::http-to-message-protobuf, test:test/http_binding_test.cpp::http-from-message-protobuf, test:test/http_binding_test.cpp::http-from-message-protobuf-refuses-other-content-type, test:test/http_binding_test.cpp::http-protobuf-content-type-ignores-case-and-parameters, test:test/http_binding_test.cpp::http-protobuf-has-no-moving-variant, test:fuzz/fuzz_protobuf_format.cpp::fuzz-protobuf-format]
owner: filip.sajdak
version: 3
---
The HTTP binding shall provide `http::to_message_protobuf(const event&) -> result<message>`, which
takes no content mode, maps one event to a message whose Content-Type is
application/cloudevents+protobuf and whose body is the bytes of `protobuf_format::encode`, and
`http::from_message_protobuf(const message&, protobuf::decode_options = {}) -> result<event>`, which
maps such a message to an event. Neither is a template, and there is no moving variant.
`http::from_message_protobuf` shall return `errc::not_a_cloudevent` for a message whose media
type, compared ignoring case and parameters, is not application/cloudevents+protobuf. The published `http::to_message` and
`http::from_message` shall not change.
