---
uid: SWR-HTTP-0018
title: The HTTP binding offers a moving receive entry point
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  The HTTP binding receives a binary-mode body that can be large and not JSON, such as an image or an
  archive, and copying it on receive costs an allocation and a pass over the bytes. SWR-BIND-0014
  states the contract the entry point shares with the other bindings; the entry point keeps the value-
  policy template parameter of http::from_message so that the two spell the same.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::from-message-moving-agrees-with-from-message]
owner: filip.sajdak
version: 1
---
The HTTP binding shall provide `http::from_message_moving<Codec, Values>`, taking a `message` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<event>`.
