---
uid: SWR-CORE-0038
title: to_v3 converts a ce::v4 event into an equal ce::v3 event
type: software
status: reviewed
priority: high
rationale: >
  Code built against ce::v4 hands events to libraries that are still on ce::v3.
  The two event models differ only in how json_document moves, so a v4 event whose payload is not a moved-from document has an exact v3 counterpart.
  The owner decided on 2026-09-28 that the conversion is infallible and returns the event rather than a result.
  The moved-from payload, which ce::v3 has no state for, is SWR-CORE-0040.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide `ce::v4::to_v3`, overloaded for `const ce::v4::event&` and `ce::v4::event&&`, returning without a `result` wrapper a `ce::v3::event` with the same context attributes, the same extensions and, unless the payload is a moved-from json_document, the same payload as the v4 event.
