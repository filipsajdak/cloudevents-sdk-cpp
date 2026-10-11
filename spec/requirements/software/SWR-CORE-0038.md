---
uid: SWR-CORE-0038
title: to_v3 converts a ce::v4 event into an equal ce::v3 event
type: software
status: implemented
delivered_in: v0.6.0
priority: high
rationale: >
  Code built against ce::v4 hands events to libraries that are still on ce::v3.
  The two event models differ only in how json_document moves, so a v4 event whose payload is not a moved-from document has an exact v3 counterpart.
  The owner decided on 2026-09-28 that the conversion is infallible and returns the event rather than a result.
  It is declared beside from_v3, in the opt-in header include/cloudevents/v3_conversion.hpp the owner chose on 2026-09-29 (SWR-CORE-0037, ADR-0012).
  The moved-from payload, which ce::v3 has no state for, is SWR-CORE-0040.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/v3_conversion.hpp]
verified_by: [test:test/generation_conversion_test.cpp::to-v3-keeps-the-event]
owner: filip.sajdak
version: 3
---
The header `cloudevents/v3_conversion.hpp` shall provide `ce::v4::to_v3`, overloaded for `const ce::v4::event&` and `ce::v4::event&&`, returning without a `result` wrapper a `ce::v3::event` with the same context attributes, the same extensions and, unless the payload is a moved-from json_document, the same payload as the v4 event.
