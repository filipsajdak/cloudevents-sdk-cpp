---
uid: SWR-CORE-0029
title: The event builder refuses to build without the required attributes
type: software
status: approved
priority: high
rationale: >
  A decoder discovers attributes one at a time in wire order and cannot know
  until the end whether `id` arrived, so it needs an accumulating shape whose only
  failure mode is absence. ADR-0008 keeps the constructor infallible for the
  producer, who already holds validated attributes, and gives the decoder a builder whose
  `build` is rvalue-ref-qualified so a half-built event cannot be built twice or
  left behind.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/event_builder_test.cpp::event-builder-requires-id-source-and-type]
owner: filip.sajdak
version: 2
---
When `build` is called before an `id`, a `source` and a `type` have all been
supplied, the builder shall return a failed result naming the first attribute
that is absent.
