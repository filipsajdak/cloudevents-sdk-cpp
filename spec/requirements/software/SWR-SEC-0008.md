---
uid: SWR-SEC-0008
title: Kafka records interoperate with the Go and Java SDKs in both directions
type: software
status: implemented
delivered_in: v0.6.1
priority: high
rationale: >
  The Kafka binding has been verified against its own tests and against the binding specification, and against no other SDK. STK-INTEROP-0001 makes agreement with the established SDKs the external correctness measure, and the HTTP binding already has it (SWR-HTTP-0016, SWR-SEC-0005). The Go and Java Kafka bindings convert an event to and from an in-memory record, a key, a list of headers and a value, with no broker in the path, so the agreement can be measured on the bytes a broker would carry. The header-name rule (D-KAFKA-2: the `ce_` prefix and lowercase, matched byte for byte) and the key rule (SWR-KAFKA-0009) are the places the SDKs are most likely to differ, and a difference that is found is recorded as a decision rather than hidden by loosening a test.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:interop/kafka/go/main.go, code:interop/kafka/java/src/main/java/interop/KafkaInterop.java, code:interop/kafka/cpp/produce.cpp]
verified_by: [test:test/interop_kafka_test.cpp::interop-kafka-records-decode, test:test/interop_kafka_test.cpp::interop-kafka-header-names, test:test/interop_kafka_test.cpp::interop-kafka-differences]
owner: filip.sajdak
version: 1
---
The SDK shall decode, in binary and in structured mode, Kafka records recorded from the Go and Java CloudEvents SDKs to the event those SDKs encoded. It shall produce records in both modes that the Go and Java SDKs decode to the event this SDK encoded, and the records, their generators and the verifiers shall be committed together, so that the agreement is reproducible without a broker. A difference between the SDKs is recorded as a decision in `docs/DECISIONS.md`, and the committed records are not edited to hide it.
