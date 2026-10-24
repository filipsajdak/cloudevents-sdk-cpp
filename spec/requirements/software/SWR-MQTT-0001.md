---
uid: SWR-MQTT-0001
title: Structured mode on MQTT 5.0 carries the event in the payload under its format media type
type: software
status: approved
priority: high
rationale: >
  Slice 1 (CR-0008). Binding text :213-244 requires the Content Type property to be the media type of
  an event format on 5.0 and the payload to be the formatted event; every implementation that supports
  structured mode must support the JSON event format (:72-73). User Properties are optional in
  structured mode (:243-244), so the binding writes none and ignores any it receives. sdk-go agrees
  (write_message.go:47 sets Properties.ContentType and the payload and no User Property), which makes
  this the part of the binding that needs no ruling.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-structured-mode-5-0, test:test/mqtt_binding_test.cpp::mqtt-structured-ignores-user-properties, test:test/mqtt_binding_test.cpp::mqtt-writes-structured-mode-only, test:test/mqtt_binding_test.cpp::mqtt-from-message-takes-decode-options]
owner: filip.sajdak
version: 1
---
The MQTT binding shall write an event in structured mode as a message whose body is the JSON event format and whose Content Type is carried in the header content-type with the value application/cloudevents+json, with no other header, and shall read such a message, ignoring every User Property, to the event that was written.
