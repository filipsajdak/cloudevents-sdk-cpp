---
uid: SWR-KAFKA-0004
title: Record header keys compare byte for byte
type: software
status: approved
priority: high
rationale: >
  A Kafka record header key is an opaque byte string, so there is no case-folding rule to apply and none is specified. HTTP's case-insensitive comparison would therefore be wrong twice over: reading, it would accept `CE_ID` as the id attribute, which the producer did not send; writing, it would erase a `CE_ID` header a caller had deliberately set alongside.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-keys-are-byte-exact]
owner: filip.sajdak
version: 1
---
The Kafka binding shall compare record header keys byte for byte when matching the
attribute prefix, when reading the content type, and when deciding whether writing
a header replaces an existing one.
