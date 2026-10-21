---
uid: SWR-BIND-0013
title: Only the binary-mode body reader honours the opt-in; every other entry point and ce::v3 ignore it
type: software
status: reviewed
priority: medium
rationale: >
  Structured decode, http::from_batch_message, nats::from_payload and the json_format decoders read
  JSON text that has to be parsed to become an event, so an opt-in about a binary-mode body has
  nothing to skip there. They accept the shared decode_options, as SWR-BIND-0009 requires, and ignore
  the field. ce::v3 binary entry points take no options and already store the body unparsed, and the
  v3 structured decoders that accept the struct ignore the field, so no v3 behaviour changes.
  json_format::from_value and from_value_as take no options. A forwarder in structured mode still pays the parse: the owner ruled on 2026-10-21 that the opt-in applies to binary mode only.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The decode options' `parse_binary_body` member shall have no effect on structured decode, on `http::from_batch_message`, on `nats::from_payload`, on the `json_format` decoders and on every `ce::v3` entry point.
