---
uid: SWR-CORE-0036
title: Moving a ce::v3 json_document copies it
type: software
status: approved
priority: high
rationale: >
  v0.5.0 declared copy operations only for json_document, and its guide said that moving a document copies it, so every document holds a DOM.
  Its suite pins this in the test "a document is never empty".
  CR-0004 moves the move operations of SWR-CORE-0035 to ce::v4, and the owner decided on 2026-09-28 that ce::v3 keeps the v0.5.0 behaviour.
  A v3 program that reads a document after moving it therefore reads the value it read against v0.5.0.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/v3/core.hpp]
verified_by: [test:test/v3/json_document_test.cpp::a document is never empty, test:test/v3/v3_generation_test.cpp::v3-document-move-copies]
owner: filip.sajdak
version: 1
---
When a `ce::v3::json_document` is moved from, the source shall still hold the document it held, so that it dumps the same JSON, yields its DOM to the codec that built it, and compares equal to the moved-to document.
