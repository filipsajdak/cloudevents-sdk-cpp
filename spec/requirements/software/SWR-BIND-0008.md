---
uid: SWR-BIND-0008
title: A binary-mode body that is not JSON under a JSON media type is a parse error
type: software
status: approved
priority: high
rationale: >
  In ce::v3 a binary-mode body under a JSON media type was stored as json_text without being parsed, so a malformed body was accepted and failed only when something read it, far from the sender.
  Structured decode refuses a document that does not parse with parse_error, and the owner decided on 2026-09-28 that binary mode in ce::v4 fails the same way.
  The body comes from an untrusted peer, and the codec's parser is the one the structured path already relies on, with its nesting limit (D-SEC-4).
  An empty body is not refused: the owner decided on 2026-09-29 that it carries no payload, as in ce::v3, and SWR-BIND-0010 states that.
  A JSON media type is one is_json_content_type accepts (SWR-CORE-0024).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binary-json-body-must-parse, test:test/http_binding_test.cpp::http-binary-mode-json-body, test:test/kafka_binding_test.cpp::kafka-binary-mode-json-body, test:test/nats_binding_test.cpp::nats-binary-mode-json-body]
owner: filip.sajdak
version: 3
---
If a binding reads a non-empty binary-mode body under a media type that `is_json_content_type` accepts and the body is not valid JSON for the decoding codec, then `from_message` shall return a failed `result` with `errc::parse_error`.
