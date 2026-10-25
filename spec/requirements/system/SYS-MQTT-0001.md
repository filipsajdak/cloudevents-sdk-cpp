---
uid: SYS-MQTT-0001
title: Events map to and from MQTT PUBLISH messages
type: system
status: approved
priority: medium
rationale: >
  MQTT is the transport of constrained and device-side estates, and the CloudEvents project specifies
  a binding for it (v1.0.2, bindings/mqtt-protocol-binding.md). The binding needs no MQTT client
  library: a PUBLISH is a payload, a Content Type property and an ordered list of User Properties,
  which is the message shape the SDK already has (docs/spikes/mqtt.md, section 4). Owner ruling
  2026-10-24 (CR-0008): MQTT is a bet, shipped as an addition in ce::v4 in two slices.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-structured-mode-5-0, test:test/mqtt_binding_test.cpp::mqtt-3-1-1-payload-round-trip]
owner: filip.sajdak
version: 1
---
The SDK shall map an event to and from an MQTT PUBLISH message in structured mode on MQTT 5.0 and 3.1.1 and in binary mode on MQTT 5.0, naming no MQTT client type, so that an event published by another SDK is read back unchanged.
