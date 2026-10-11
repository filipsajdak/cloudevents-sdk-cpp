---
uid: SWR-BIND-0007
title: A binary-mode JSON body above the retention limit is kept as its own text
type: software
status: implemented
delivered_in: v0.6.0
priority: high
rationale: >
  In ce::v4 a JSON body received in binary mode is parsed into a json_document (SWR-BIND-0006), and a retained DOM can occupy several times its text for the life of the event.
  Structured decode bounds that with the retention limit of SWR-JSON-0040, and the owner decided on 2026-09-28 that binary mode applies the same limit with the same 16 KiB default.
  In binary mode the body is the payload and nothing else (SWR-HTTP-0009), so its length is the payload's length and its bytes are the payload's own text.
  The owner decided on 2026-09-29 that the stored text is the body's bytes exactly as received; structured decode trims the whitespace around a member (SWR-JSON-0043), but a binary-mode body has no surrounding document to trim it from.
  A JSON media type is one is_json_content_type accepts (SWR-CORE-0024), the predicate the body reader already applies.
  The body is still parsed above the limit, so a malformed body fails the same way at any size (SWR-BIND-0008).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp, code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/binding_core_test.cpp::binary-json-body-above-the-limit-stays-text, test:test/http_binding_test.cpp::http-binary-mode-json-body]
owner: filip.sajdak
version: 3
---
When a binding reads a binary-mode body under a media type that `is_json_content_type` accepts and the body is longer than the retention limit, it shall store the body's bytes, exactly as received, as `json_text` instead of a json_document.
