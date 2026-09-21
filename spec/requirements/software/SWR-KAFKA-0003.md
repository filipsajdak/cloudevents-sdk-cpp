---
uid: SWR-KAFKA-0003
title: Header values are UTF-8 strings, not percent-encoded
type: software
status: approved
priority: high
rationale: >
  The Kafka binding specification section 3.2.1 states that both header keys and header values MUST be encoded as UTF-8 strings. There is no escaping layer, so percent-encoding a value here would put literal percent escapes in front of every other SDK's consumer. A broker passes header bytes through unexamined, so nothing downstream would reject a value that is not well-formed UTF-8 either; this binding refuses it rather than emitting it.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-header-values-are-utf8]
owner: filip.sajdak
version: 1
---
The Kafka binding shall write each header value as the attribute's UTF-8 text form
without escaping, and shall report a value that is not well-formed UTF-8 as
`errc::invalid_utf8` rather than transmitting it.
