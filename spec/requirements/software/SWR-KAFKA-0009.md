---
uid: SWR-KAFKA-0009
title: partitionkey still travels when it becomes the record key
type: software
status: approved
priority: high
rationale: >
  The Kafka binding specification section 3.1 states that the `partitionkey` attribute MUST still be included with the transmitted event if present. Moving it to the record key instead of copying it would lose the attribute for every consumer that reads the event rather than the record, and the mistake is invisible in a test that only inspects the key.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-record-and-key-mapper]
owner: filip.sajdak
version: 1
---
Where a key mapper takes the record key from the `partitionkey` extension, the
Kafka binding shall still emit that extension as a record header and shall leave
the caller's event unchanged.
