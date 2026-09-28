---
uid: SWR-BIND-0007
title: A binary-mode JSON body above the retention limit is kept as its own text
type: software
status: reviewed
priority: high
rationale: >
  In ce::v4 a JSON body received in binary mode is parsed into a json_document (SWR-BIND-0006), and a retained DOM can occupy several times its text for the life of the event.
  Structured decode bounds that with the retention limit of SWR-JSON-0040, and the owner decided on 2026-09-28 that binary mode applies the same limit with the same 16 KiB default.
  In binary mode the body is the payload and nothing else (SWR-HTTP-0009), so its length is the payload's length and its bytes are the payload's own text.
  The body is still parsed above the limit, so a malformed body fails the same way at any size (SWR-BIND-0008).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a binding reads a binary-mode body under a JSON media type and the body is longer than the retention limit, it shall store the body's bytes as `json_text` instead of a json_document.
