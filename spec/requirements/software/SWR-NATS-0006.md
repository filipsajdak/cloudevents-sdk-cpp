---
uid: SWR-NATS-0006
title: The NATS binding offers a moving payload receive entry point
type: software
status: reviewed
priority: medium
rationale: >
  The owner chose on 2026-10-21 that nats::from_payload has a moving counterpart, so that the payload
  path matches the message path although a payload parse saves nothing from a move (CR-0005, informed
  choice of symmetry). The copying entry point takes a std::string_view, which owns nothing, so the
  moving one takes an owning std::string, the type nats::to_payload returns; the owner ruled the
  parameter type and the name from_payload_moving on 2026-10-21. A payload is JSON, which a call reads
  and does not adopt, so the entry point always takes the copying path and never modifies the string.
  SWR-BIND-0018 states the contract.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::payload-moving-agrees-with-the-copying-payload, test:test/nats_binding_test.cpp::payload-moving-never-modifies-the-string]
owner: filip.sajdak
version: 1
---
The NATS binding shall provide `nats::from_payload_moving<Codec>`, taking a `std::string` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<event>`.
