---
uid: SWR-BIND-0010
title: An empty binary-mode body carries no payload, whatever its media type
type: software
status: reviewed
priority: medium
rationale: >
  In ce::v4 a binary-mode body under a JSON media type is parsed (SWR-BIND-0006), and a body that does not parse is a parse error (SWR-BIND-0008).
  An empty body is not JSON, so without a statement of its own it would fall to that refusal.
  The owner decided on 2026-09-29 that it carries no payload, as it does in ce::v3, whose body reader returns no payload for an empty body before it looks at the media type.
  A sender that declares application/json on an event without data therefore still round-trips, and the rule does not depend on which media types are JSON.
  The owner declined parse_error, which would refuse such events on receive although they are valid CloudEvents.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a binding reads an empty binary-mode body, it shall return an event with no payload, whether or not its media type is one `is_json_content_type` accepts.
