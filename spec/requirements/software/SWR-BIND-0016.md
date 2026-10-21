---
uid: SWR-BIND-0016
title: A failed moving call leaves the message unchanged
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  A forwarder that cannot decode a message may still need to send its bytes elsewhere, for example to
  a dead-letter queue, so a refusal must not have consumed the body. The owner ruled on 2026-10-21
  that a failed call leaves the message unchanged, for every body. The body is therefore taken only after the last
  step that can fail, and the implementing stage must order the take after the event's final
  validation; if that cannot be done without a copy, the stage reports it and the owner decides
  (CR-0005).
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp, code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/kafka.hpp, code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/binding_core_test.cpp::failed-moving-call-leaves-the-message-unchanged, test:test/http_binding_test.cpp::http-moving-call-failure-keeps-the-message, test:test/kafka_binding_test.cpp::kafka-moving-call-failure-keeps-the-message, test:test/nats_binding_test.cpp::nats-moving-call-failure-keeps-the-message]
owner: filip.sajdak
version: 1
---
When a moving entry point returns a failed result, the message passed to it shall hold the headers and the body it held before the call.
