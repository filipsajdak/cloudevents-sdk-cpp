---
uid: SWR-MQTT-0002
title: MQTT 3.1.1 carries structured JSON in the payload and has no properties
type: software
status: reviewed
priority: high
rationale: >
  Slice 1 (CR-0008). Binding text :135-136: with MQTT 3.1.1 the content mode is always structured and
  the payload must use the JSON event format; 3.1.1 has no Content Type and no User Properties. A
  message with no headers cannot be told from a 5.0 message with no properties, so the caller names
  the protocol version by choosing the entry point: a from_message that guessed would break the rule
  on one side (docs/spikes/mqtt.md, open decisions). The shape follows nats::to_payload and
  nats::from_payload (D-NATS-2).
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The MQTT binding shall provide payload entry points that write an event as the bytes of its JSON event format and read such bytes to an event, for MQTT 3.1.1, and shall read the payload whatever headers a message carries when it is called through those entry points.
