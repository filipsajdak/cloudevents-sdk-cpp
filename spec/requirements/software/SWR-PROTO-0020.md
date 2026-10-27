---
uid: SWR-PROTO-0020
title: kafka structured mode in the Protobuf format
type: software
status: approved
priority: medium
rationale: >
  As SWR-PROTO-0019, for a Kafka record, whose content-type header carries the media type. The names and the
  content type check are rulings 23 and 25 of 2026-10-27 (CR-0009).
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp, code:include/cloudevents/binding/detail/protobuf_structured.hpp, code:include/cloudevents/cloudevents.cppm]
verified_by: [test:test/kafka_binding_test.cpp::kafka-to-message-protobuf, test:test/kafka_binding_test.cpp::kafka-from-message-protobuf, test:test/kafka_binding_test.cpp::kafka-from-message-protobuf-refuses-other-content-type, test:test/kafka_binding_test.cpp::kafka-protobuf-content-type-ignores-case-and-parameters, test:test/kafka_binding_test.cpp::kafka-to-message-protobuf-sets-no-key, test:test/kafka_binding_test.cpp::kafka-protobuf-has-no-moving-variant, test:fuzz/fuzz_protobuf_format.cpp::fuzz-protobuf-format, test:test/module_test.cpp::module-exports-protobuf-bindings]
owner: filip.sajdak
version: 3
---
The Kafka binding shall provide `kafka::to_message_protobuf(const event&) -> result<message>`,
which takes no content mode, sets no key, and maps one event to a record whose content-type
header is application/cloudevents+protobuf and whose value is the bytes of
`protobuf_format::encode`, and
`kafka::from_message_protobuf(const message&, protobuf::decode_options = {}) -> result<event>`,
which maps such a record to an event. Neither is a template, and there is no moving variant.
`kafka::from_message_protobuf` shall return `errc::not_a_cloudevent` for a record whose
content-type header media type, compared ignoring case and parameters, is not
application/cloudevents+protobuf. The published `kafka::to_message`
and `kafka::from_message` shall not change.
