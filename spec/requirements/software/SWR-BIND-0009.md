---
uid: SWR-BIND-0009
title: from_message takes the JSON decode options for every content mode
type: software
status: reviewed
priority: medium
rationale: >
  In ce::v3 a binding's from_message decoded a structured message with the default decode options and gave the caller no way to set the retention limit.
  In ce::v4 the binary path parses JSON bodies as well (SWR-BIND-0006), so both paths retain documents and both need the limit.
  The owner decided on 2026-09-28 that from_message takes json::decode_options with the same 16 KiB default.
  One parameter for both paths means a caller that lowers the limit for untrusted input cannot forget one content mode.
  A defaulted last parameter, as json_format::decode takes it (D-JSON-5), keeps every existing call spelled the same.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The HTTP, Kafka and NATS bindings shall each provide `from_message<Codec>` taking a `json::decode_options` argument, defaulting to a retention limit of 16 KiB, and shall apply it to the JSON payload of both a structured-mode and a binary-mode message.
