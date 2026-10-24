---
uid: SWR-PROTO-0022
title: The JSON entry points do not change
type: software
status: reviewed
priority: medium
rationale: >
  Structured mode is hard-wired to JSON today. The Protobuf entry points are new names (the
  approach of CR-0005, ruling 12), so no call of a published entry point changes meaning.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The published entry points of the HTTP, Kafka and NATS bindings shall return for a given message
and options the result it returned at v0.6.1, including for a message whose content type is
application/cloudevents+protobuf.
