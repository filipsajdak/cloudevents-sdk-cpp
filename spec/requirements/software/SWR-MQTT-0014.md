---
uid: SWR-MQTT-0014
title: Structured MQTT messages interoperate with the Go SDK in both directions
type: software
status: reviewed
priority: high
rationale: >
  Slice 1 (CR-0008). STK-INTEROP-0001 makes agreement with the established SDKs the external measure,
  and SWR-SEC-0008 did it for Kafka. sdk-go (protocol/mqtt_paho/v2) converts an event to and from a
  caller-supplied paho Publish without a client (write_message.go:22, message.go:39), so 16 goldens
  exist offline (interop/mqtt-spike/, PR 112). sdk-java has no MQTT binding (README row not supported;
  no mqtt path at 81d64ac), so the evidence has one peer, not two. Not verified by the spike: the MQTT
  packet bytes, and any broker.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall decode the structured-mode messages recorded from the Go CloudEvents SDK to the event that SDK encoded and shall produce structured-mode messages that the Go SDK decodes to the event this SDK encoded, with the recorded messages, their generator and the verifier committed together so that the agreement is reproducible without a broker.
