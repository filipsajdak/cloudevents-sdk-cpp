---
uid: SWR-JSON-0040
title: A document above the retention limit keeps its payload as text
type: software
status: approved
priority: high
rationale: >
  A parsed DOM can occupy many times the bytes of its text, and a retained one lives as long as the event.
  Without a bound, a small hostile document could pin a large expansion in memory.
  The limit is a decode option, 64 KiB by default, and 0 keeps every payload as text.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::decode-retains-document-up-to-limit]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a document whose text is longer than the retention limit, it shall store a JSON payload as json_text instead of json_document.
