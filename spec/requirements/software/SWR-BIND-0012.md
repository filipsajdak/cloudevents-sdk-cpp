---
uid: SWR-BIND-0012
title: With the opt-in off, a binary-mode JSON body is kept as its own unparsed text
type: software
status: implemented
delivered_in: v0.6.1
priority: high
rationale: >
  A forwarder that sets parse_binary_body to false wants the body untouched and no parse, so the body
  is stored as json_text holding its bytes exactly as received (SWR-BIND-0007 states the same bytes
  above the limit), without validation. It never fails with parse_error, whatever its size, and the
  retention limit does not apply, because nothing is retained as a document. The caller takes over
  validation of a body it chose not to parse, and the default stays validating (SWR-BIND-0008). An
  empty body still carries no payload (SWR-BIND-0010), and a body under another media type is still
  carried as bytes. The cost target is the ce::v3 path, which stores the body the same way.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::unparsed-binary-json-body-keeps-the-bytes, test:test/binding_core_test.cpp::unparsed-binary-json-body-never-fails-to-parse, test:test/binding_core_test.cpp::unparsed-binary-json-body-ignores-the-retention-limit, test:test/http_binding_test.cpp::http-binary-body-not-parsed, test:test/kafka_binding_test.cpp::kafka-binary-body-not-parsed, test:test/nats_binding_test.cpp::nats-binary-body-not-parsed]
owner: filip.sajdak
version: 1
---
When `parse_binary_body` is false and a binding reads a non-empty binary-mode body under a media type that `is_json_content_type` accepts, the binding shall store the body's bytes, exactly as received and without parsing or validating them, as `json_text`, whatever the length of the body and the retention limit.
