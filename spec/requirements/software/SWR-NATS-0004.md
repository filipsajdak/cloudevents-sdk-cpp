---
uid: SWR-NATS-0004
title: NATS cannot distinguish a foreign payload from a malformed one
type: software
status: approved
priority: medium
rationale: >
  The HTTP and Kafka bindings answer `errc::not_a_cloudevent` because each has a content type or a specversion header to consult before parsing. A bare NATS payload carries neither, so an unrelated JSON document is indistinguishable from a corrupt event. Stating the limitation is the requirement, because the alternative is a bug report asking for a distinction the transport cannot support. It applies to the payload entry points only: a binary-mode message carries `ce-specversion`, so the distinction exists there.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-every-failure-is-a-parse-error]
owner: filip.sajdak
version: 2
---
When a payload passed to the structured entry points is not a well-formed
CloudEvent, the binding shall report the parse or validation failure and shall
not report `errc::not_a_cloudevent`.
