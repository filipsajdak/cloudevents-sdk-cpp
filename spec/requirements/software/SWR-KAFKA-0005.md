---
uid: SWR-KAFKA-0005
title: The Kafka binding refuses batch mode in both directions
type: software
status: approved
priority: high
rationale: >
  The Kafka binding specification defines binary and structured mode and no batch mode. A record carrying an array under a batch content type is something no other SDK's consumer reads, so producing one would be an interoperability defect rather than an extension. On receive the batch content type must be recognised before the structured one, because `application/cloudevents-batch+json` also starts with `application/cloudevents` and would otherwise reach the format layer as a malformed structured document.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-batch-refused]
owner: filip.sajdak
version: 1
---
When a caller asks for batch mode, or a received record declares a batch content
type, the Kafka binding shall report `errc::invalid_argument` naming the absence of
a batch mode rather than a parse failure.
