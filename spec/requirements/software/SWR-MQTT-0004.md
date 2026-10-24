---
uid: SWR-MQTT-0004
title: The mode is read from the Content Type, then from the specversion User Property
type: software
status: reviewed
priority: high
rationale: >
  Slice 1 for the structured branch, slice 2 for the binary one (CR-0008). Binding text :114-119: a
  Content Type prefixed application/cloudevents is structured, otherwise binary. sdk-go tests the
  Content Type first (message.go:43) and takes binary only when the version attribute is present
  (:45), else EncodingUnknown (:56-63); kafka::from_message does the same. The text alone would read a
  message with no version attribute as binary and then fail to build an event; the outcomes differ
  only in the error.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a received message has a Content Type starting with application/cloudevents the MQTT binding shall read it as structured mode, and otherwise it shall read it as binary mode only when it carries the specversion attribute as a User Property under the name rule in force.
