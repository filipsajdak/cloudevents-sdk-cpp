---
uid: SWR-HTTP-0019
title: The HTTP binding offers a moving batch receive entry point
type: software
status: reviewed
priority: medium
rationale: >
  The owner chose on 2026-10-21 that the batch entry point has a moving counterpart beside the
  single-event one, so that every HTTP receive has both forms. A batch parse reads the body and keeps
  none of it apart from copied slices, so the move saves nothing; the owner was told and chose the
  symmetry (CR-0005). It always takes the copying path and never modifies its message. SWR-BIND-0018
  states the contract, and the owner ruled the name.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::batch-moving-agrees-with-the-copying-batch, test:test/http_binding_test.cpp::batch-moving-never-modifies-the-message]
owner: filip.sajdak
version: 1
---
The HTTP binding shall provide `http::from_batch_message_moving<Codec>`, taking a `message` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<std::vector<event>>`.
