---
uid: SWR-KAFKA-0008
title: Key mapping is opt-in and defaults to no key
type: software
status: approved
priority: medium
rationale: >
  The Kafka binding specification section 3.1 states that every implementation SHOULD provide an opt-in Key Mapper that maps the `partitionkey` attribute to the record key. Opt-in is structural here rather than a runtime flag: the mapper is a defaulted template parameter whose default produces no key, so a caller who has not asked gets the specification's default behaviour and pays nothing for the one they did not choose.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-record-and-key-mapper]
owner: filip.sajdak
version: 1
---
The Kafka binding shall produce a record with no key unless the caller names a key
mapper, and shall offer a mapper that takes the key from the `partitionkey`
extension when one is present.
