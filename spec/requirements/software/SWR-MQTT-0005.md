---
uid: SWR-MQTT-0005
title: A message that is not a CloudEvent is distinct from a malformed one
type: software
status: reviewed
priority: high
rationale: >
  Slice 1 (CR-0008). A topic carries whatever its publishers put there, so a subscriber meets a plain
  message far more often than a corrupt event; the Kafka binding keeps the two outcomes apart
  (SWR-KAFKA-0006) and so does HTTP. Without a version attribute and without a CloudEvents Content
  Type the answer is not_a_cloudevent; a structured payload that does not parse, or a binary event
  that fails validation, is the codec's or the core's error.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
If a received message has neither a CloudEvents Content Type nor the specversion User Property, then the MQTT binding shall report errc::not_a_cloudevent, and if the event it carries is malformed it shall report the error of the codec or of the core, such as errc::parse_error.
