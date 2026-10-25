---
uid: SWR-MQTT-0007
title: User Property names compare byte for byte
type: software
status: approved
priority: medium
rationale: >
  Slice 1 for the content-type lookup, slice 2 for attribute names (CR-0008). An MQTT User Property
  name is a UTF-8 string with no case-folding rule, so HTTP's case-insensitive comparison would be
  wrong twice over, as SWR-KAFKA-0004 argues for Kafka; sdk-go refuses CE-specversion
  (docs/spikes/mqtt.md, section 3). The core's case_sensitive_names trait states this.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-names-are-byte-exact]
owner: filip.sajdak
version: 1
---
The MQTT binding shall compare User Property names and the content-type header name byte for byte when reading the content type, when matching attribute names and when deciding whether writing a header replaces an existing one.
