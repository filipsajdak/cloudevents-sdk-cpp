---
uid: SWR-PROTO-0023
title: Protobuf-encoded messages interoperate through the bindings
type: software
status: approved
priority: medium
rationale: >
  The Go and Java SDKs carry the Protobuf event as the body of an HTTP request and the value of
  a Kafka record. The goldens of SWR-PROTO-0016 are the bytes of that body, and the message form
  is checked against the SDKs' bindings offline.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0002]
satisfied_by: [code:interop/protobuf/go/main.go, code:interop/protobuf/go/messages.go, code:interop/protobuf/java/src/main/java/interop/ProtobufInterop.java, code:interop/protobuf/cpp/produce.cpp]
verified_by: [test:test/interop_protobuf_test.cpp::interop-protobuf-http-body, test:test/interop_protobuf_test.cpp::interop-protobuf-kafka-value]
owner: filip.sajdak
version: 2
---
The SDK shall decode, through the `from_message_protobuf` entry point of each of the HTTP and
Kafka bindings, the structured-mode messages recorded from the Go
and Java SDKs with the Protobuf format, and shall produce messages that those SDKs decode to the
event this SDK encoded (through `to_message_protobuf`), with the recordings, generators and verifiers committed together.
