---
uid: SWR-CORE-0035
title: A json_document moves without a reference-count operation
type: software
status: implemented
delivered_in: v0.6.0
priority: medium
rationale: >
  An event is moved more often than it is copied: decode moves the event it builds into its result, and a batch moves each event into its vector.
  json_document declared only copy operations, so each of those moves copied its shared pointer, which is one atomic increment and, when the source is destroyed, one atomic decrement.
  A move now takes the source's pointer and leaves the source pointing at one static, immutable model that is not reference counted, so moving performs no atomic operation.
  The moved-from document is still not empty, so SWR-CORE-0031 holds: every member can be called on it.
  It holds JSON null, so dump returns null.
  No codec built it, and a codec's identity is never empty (SWR-JSON-0039), so get returns nullptr for every codec.
  It compares equal only to another moved-from document, because it holds no codec's value for a codec's equal to compare.
  Comparing it as JSON null instead would make an event whose payload was moved away equal to one carrying a null payload, hiding a use after move behind a passing comparison.
  The static model never changes after it is built, so threads that copy, move different objects and read stay free of data races (SWR-CORE-0032).
  The owner decided the moved-from state on 2026-09-27.
  v0.5.0 documented that moving a document copies it, and the owner ruled on 2026-09-28 (CR-0004) that changing that documented behaviour is a break under SWR-BUILD-0006, so the move is a ce::v4 feature and ce::v3 keeps the copy (SWR-CORE-0036).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp, code:include/cloudevents/detail/json_document_model.hpp]
verified_by: [test:test/json_document_test.cpp::json-document-moves-without-counting, test:test/json_document_test.cpp::threads move their copies while others copy the same document]
owner: filip.sajdak
version: 2
---
The `json_document` type shall provide move construction and move assignment that transfer the source's document without changing its reference count and leave the source a moved-from document, whose `dump()` returns `null`, whose `get<Codec>()` returns `nullptr` for every codec, and which compares equal only to another moved-from document.
