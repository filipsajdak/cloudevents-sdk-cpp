---
uid: SWR-PROTO-0021
title: nats structured mode in the Protobuf format
type: software
status: approved
priority: medium
rationale: >
  As SWR-PROTO-0019, for a NATS message. A NATS payload has no content type of its own, so the
  name of the entry point carries the format (CR-0009, ruling 24 of 2026-10-27): there is no tag
  argument and no overload of a published function.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: [code:include/cloudevents/binding/nats.hpp, code:include/cloudevents/binding/detail/protobuf_structured.hpp, code:include/cloudevents/cloudevents.cppm]
verified_by: [test:test/nats_binding_test.cpp::nats-to-message-protobuf, test:test/nats_binding_test.cpp::nats-from-message-protobuf, test:test/nats_binding_test.cpp::nats-from-message-protobuf-refuses-other-content-type, test:test/nats_binding_test.cpp::nats-to-payload-protobuf, test:test/nats_binding_test.cpp::nats-from-payload-protobuf-always-reads-protobuf, test:test/nats_binding_test.cpp::nats-from-payload-protobuf-garbage-is-parse-error, test:fuzz/fuzz_protobuf_format.cpp::fuzz-protobuf-format, test:test/module_test.cpp::module-exports-protobuf-bindings]
owner: filip.sajdak
version: 3
---
The NATS binding shall provide, for the message form,
`nats::to_message_protobuf(const event&) -> result<message>`, which takes no content mode and
writes a Content-Type header of application/cloudevents+protobuf, and
`nats::from_message_protobuf(const message&, protobuf::decode_options = {}) -> result<event>`; and,
for the payload form, `nats::to_payload_protobuf(const event&) -> result<binary>` and
`nats::from_payload_protobuf(std::span<const std::byte>, protobuf::decode_options = {}) ->
result<event>`, whose bytes are those of `protobuf_format::encode`. None is a template and none
has a moving variant. `nats::from_message_protobuf` shall return `errc::not_a_cloudevent` for a
message whose Content-Type media type, compared ignoring case and parameters, is not
application/cloudevents+protobuf. `nats::from_payload_protobuf` shall always read its argument as
Protobuf, with no argument naming the format, and shall decode any bytes it is given, returning
`errc::parse_error` for bytes that are not an event. The
published `nats::to_payload`, `nats::from_payload`, `nats::to_message` and `nats::from_message`
shall not change.
