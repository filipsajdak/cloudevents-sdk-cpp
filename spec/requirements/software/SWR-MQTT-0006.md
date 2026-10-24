---
uid: SWR-MQTT-0006
title: The MQTT binding refuses batch mode in both directions
type: software
status: reviewed
priority: medium
rationale: >
  Slice 1 (CR-0008). The binding text defines no batch mode, and neither does sdk-go
  (docs/spikes/mqtt.md, section 1); the Kafka binding refuses it the same way (SWR-KAFKA-0005). A
  batch media type on receive is refused, not read as a single event.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The MQTT binding shall report errc::invalid_argument when asked to write a batch and when a received message has a Content Type starting with application/cloudevents-batch.
