---
uid: SYS-PROTO-0002
title: Protobuf structured mode in the protocol bindings
type: system
status: approved
priority: medium
rationale: >
  The Protobuf format is carried by a transport as the body of a message with the media type
  application/cloudevents+protobuf. The bindings map to and from a transport-neutral message
  (ADR-0005), so the Protobuf format needs no transport type. This is the second slice of
  CR-0009 and follows the format-only slice.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/kafka.hpp, code:include/cloudevents/binding/nats.hpp, code:include/cloudevents/binding/detail/protobuf_structured.hpp]
verified_by: [test:test/http_binding_test.cpp::http-from-message-protobuf, test:test/kafka_binding_test.cpp::kafka-from-message-protobuf, test:test/nats_binding_test.cpp::nats-from-message-protobuf, test:test/nats_binding_test.cpp::nats-from-payload-protobuf-always-reads-protobuf, test:test/binding_core_test.cpp::protobuf-content-type-does-not-change-json-entry-points, test:test/interop_protobuf_test.cpp::interop-protobuf-http-body, test:test/interop_protobuf_test.cpp::interop-protobuf-kafka-value]
owner: filip.sajdak
version: 1
---
The SDK shall map an event to and from the HTTP, Kafka and NATS message in structured mode using
the Protobuf event format, naming no transport type, so that a Protobuf-encoded event written by
another SDK onto those transports is read back unchanged.
