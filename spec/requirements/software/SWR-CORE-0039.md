---
uid: SWR-CORE-0039
title: A converted json_document shares its model and copies no DOM
type: software
status: approved
priority: high
rationale: >
  A retained document can occupy several times its text (SWR-JSON-0040), and copying a RapidJSON value needs the codec's copy.
  The document model is declared once, in ce::v3::detail, and both generations' json_document point at it (ADR-0012), so a conversion can hand over the pointer instead of the DOM.
  The model is immutable once built (SWR-CORE-0032), so a v3 and a v4 document sharing it can be read from different threads without a data race.
  The owner decided on 2026-09-28 that a document converts in constant time.
  The conversions are declared in include/cloudevents/v3_conversion.hpp (SWR-CORE-0037), which reaches both models through the one detail accessor ADR-0012 describes.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/v3_conversion.hpp]
verified_by: [test:test/generation_conversion_test.cpp::conversion-shares-the-document]
owner: filip.sajdak
version: 2
---
When `from_v3` or `to_v3` converts a payload held as a json_document, the converted document shall share the source document's model, so that no DOM is copied, serialised or parsed and `get<Codec>()` on both returns the same address.
