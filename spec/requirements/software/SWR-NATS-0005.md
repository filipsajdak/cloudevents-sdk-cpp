---
uid: SWR-NATS-0005
title: The NATS binding offers a moving receive entry point
type: software
status: reviewed
priority: medium
rationale: >
  A NATS message in binary mode can carry a large non-JSON body, and moving it on receive saves a
  copy. nats::from_payload has a moving counterpart of its own, SWR-NATS-0006, for the payload that is
  not a message. SWR-BIND-0014 states the shared contract.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The NATS binding shall provide `nats::from_message_moving<Codec>`, taking a `message` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<event>`.
