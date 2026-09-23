---
uid: SWR-EXT-0004
title: Typed view over an event payload
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.5 requires event_of<T> as a thin typed view whose data() returns result<T>,
  giving a producer and a consumer a shared compile-time contract about the payload
  rather than an agreement recorded only in documentation.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/extensions_test.cpp::event-of-typed-view]
owner: filip.sajdak
version: 1
---
The SDK shall provide a typed view over an event whose data accessor returns the
described payload type or a typed error.
