---
uid: SWR-KAFKA-0001
title: Attributes travel as ce_-prefixed record headers
type: software
status: approved
priority: high
rationale: >
  The Kafka binding specification section 3.2.3 states that CloudEvent attributes are prefixed with `ce_` for use in the message-headers section. The underscore is the whole difference from HTTP's `ce-`, which makes it the easiest thing to carry over by accident and the hardest to see in review.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-binary-mode]
owner: filip.sajdak
version: 1
---
The Kafka binding shall carry each event attribute in binary mode as a record
header whose key is the attribute name prefixed with `ce_`.
