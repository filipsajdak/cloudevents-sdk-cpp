---
uid: SWR-MQTT-0011
title: User Property values are canonical strings, valid UTF-8 without NUL
type: software
status: approved
priority: medium
rationale: >
  Slice 2 (CR-0008). Binding text :173-175: values are the attribute type's canonical string
  representation. An MQTT UTF-8 Encoded String must be well-formed UTF-8 and must not contain U+0000
  (OASIS MQTT 5.0, 1.5.4), and its two-byte length prefix limits it to 65,535 bytes (1.5.4, read
  2026-10-26 for slice 2). The same rules apply on read, so a message an adapter built in memory is
  held to what the wire could carry. There is no percent-encoding, as in the Kafka binding.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-values-canonical, test:test/mqtt_binding_test.cpp::mqtt-value-utf8-and-nul]
owner: filip.sajdak
version: 1
---
The MQTT binding shall write each attribute value in its canonical string form, report errc::invalid_utf8 for a value that is not well-formed UTF-8, and report errc::invalid_argument for a value that contains NUL or is longer than 65,535 bytes, on writing and on reading.
