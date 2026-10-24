---
uid: SWR-MQTT-0013
title: A foreign User Property in binary mode is ignored
type: software
status: reviewed
priority: medium
rationale: >
  Slice 2 (CR-0008). OPEN QUESTION 1, proposal only: the text does not say what a receiver does with a
  User Property that is not an attribute, such as x-request-id (not a valid attribute name). Proposal:
  ignore it. The alternative, refusing the message, would make every broker-side annotation fatal.
  sdk-go ignores every key without the ce- prefix (message.go:82-89). Under the prefixed-names policy
  a name without ce- is ignored in every case. The owner decides; this statement changes with the
  ruling.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
If a binary-mode message carries a User Property whose name is not a valid attribute name, then the MQTT binding shall ignore that User Property and read the rest of the message.
