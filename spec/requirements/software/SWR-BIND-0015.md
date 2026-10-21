---
uid: SWR-BIND-0015
title: After a successful moving call a moved body is empty with no capacity and any other body is left intact
type: software
status: reviewed
priority: medium
rationale: >
  A caller of a moving entry point gives up a body the call moves, and the contract says what remains:
  headers have no extraction interface, so they stay as they were, and a moved body has size 0 and
  capacity 0. The owner ruled this on 2026-10-21, declined the alternative of a body that is valid and
  unspecified as a moved-from std::vector is, and ruled that the buffer is released. It is a
  deliberate and permanent promise, stronger than emptiness: the implementation moves out of the
  vector and leaves capacity 0 itself, for example with std::exchange or a swap with an empty vector,
  and a later change cannot give it up without a rule 4 decision (CR-0005). It follows the lesson of
  the moved-from json_document, where a documented moved-from state became part of the API (D-CORE-9,
  CR-0004). The owner chose to empty only what was moved: a body the call copied and parsed (a JSON
  media type, a structured message, a batch, an empty body) is left byte for byte as it was, because
  emptying it would discard the caller's bytes for no saving (SWR-BIND-0014). An empty body counts as
  not moved, so an empty vector that had capacity keeps it. A test can state it: after a successful
  call on a non-empty body under a non-JSON media type, the body has size 0 and capacity 0 and the
  headers equal their values before the call; after a successful call on any other body the message
  equals the copy taken before the call.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::moved-body-has-size-0-and-capacity-0-and-headers-are-unchanged]
owner: filip.sajdak
version: 2
---
When a moving entry point returns a successful result, the message passed to it shall hold the headers it held before the call, shall hold a body of size 0 and capacity 0 if the call moved the body, and shall hold the body it held before the call if the call did not move it.
