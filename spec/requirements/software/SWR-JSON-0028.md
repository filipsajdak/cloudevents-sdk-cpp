---
uid: SWR-JSON-0028
title: base64 decode rejects invalid input
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 requires the decoder to reject invalid input, which matters because base64 text on a decode path arrives from an untrusted peer.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/base64.hpp]
verified_by: [test:test/base64_test.cpp::base64-decode-rejects-invalid-input]
owner: filip.sajdak
version: 1
---
When base64 decode reads input containing a character outside the RFC 4648 section 4 alphabet or a length that no encoded octet sequence can produce, it shall return an error result.
