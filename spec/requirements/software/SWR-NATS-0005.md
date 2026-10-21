---
uid: SWR-NATS-0005
title: The NATS binding offers a moving receive entry point
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  A NATS message in binary mode can carry a large non-JSON body, and moving it on receive saves a
  copy. nats::from_payload has a moving counterpart of its own, SWR-NATS-0006, for the payload that is
  not a message. SWR-BIND-0014 states the shared contract.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-from-message-moving-agrees-with-from-message]
owner: filip.sajdak
version: 1
---
The NATS binding shall provide `nats::from_message_moving<Codec>`, taking a `message` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<event>`.
