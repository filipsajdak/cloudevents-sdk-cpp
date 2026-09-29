---
uid: SWR-CORE-0037
title: from_v3 converts a ce::v3 event into an equal ce::v4 event
type: software
status: reviewed
priority: high
rationale: >
  A service moving to ce::v4 still exchanges events with code built against ce::v3.
  Converting through the JSON format would cost a serialisation and a parse per event and a DOM copy per document.
  The context attributes, the extensions and every data_t alternative except json_document are types the two generations share (ADR-0012), so an event converts by copying or moving its parts.
  Every v3 event is a valid v4 event, so the conversion cannot fail, and the owner decided on 2026-09-28 that it returns the event rather than a result.
  A const reference overload copies and an rvalue overload moves, so a caller that no longer needs the v3 event pays for no copy.
  The owner decided on 2026-09-29 that the conversions live in an opt-in header of their own, include/cloudevents/v3_conversion.hpp, which no other SDK header includes, so a translation unit that uses only ce::v4 does not compile the ce::v3 event model (ADR-0012).
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The header `cloudevents/v3_conversion.hpp` shall provide `ce::v4::from_v3`, overloaded for `const ce::v3::event&` and `ce::v3::event&&`, returning without a `result` wrapper a `ce::v4::event` with the same context attributes, the same extensions and the same payload as the v3 event.
