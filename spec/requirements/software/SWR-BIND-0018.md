---
uid: SWR-BIND-0018
title: The batch and payload moving entry points return what the copying ones return and never modify their input
type: software
status: reviewed
priority: medium
rationale: >
  The owner ruled on 2026-10-21 that http::from_batch_message and nats::from_payload get moving
  counterparts too, although both parse JSON and save nothing from a move (CR-0005, rulings 14 to 17).
  The owner was told so and chose symmetry: a receiver then finds a moving form wherever it receives a
  message or a payload. The input of both is JSON, which a call reads and does not adopt, so neither
  moves anything, and the owner ruled that a body that was not moved is left intact (SWR-BIND-0015):
  the two forms always take the copying path and never modify their input, on success or on failure.
  They exist for symmetry only. from_payload takes a std::string_view that owns nothing, so its
  counterpart takes a std::string&&, as the owner ruled. The names are ruled too (CR-0005).
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/http_binding_test.cpp::batch-moving-agrees-with-the-copying-batch, test:test/http_binding_test.cpp::batch-moving-never-modifies-the-message, test:test/nats_binding_test.cpp::payload-moving-agrees-with-the-copying-payload, test:test/nats_binding_test.cpp::payload-moving-never-modifies-the-string]
owner: filip.sajdak
version: 1
---
The entry points `http::from_batch_message_moving` and `nats::from_payload_moving` shall return a result equal to the one `http::from_batch_message` and `nats::from_payload` return for equal input and equal decode options, shall leave the input they were given unchanged, after a successful and after a failed result.
