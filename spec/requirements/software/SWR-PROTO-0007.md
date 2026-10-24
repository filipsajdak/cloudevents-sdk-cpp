---
uid: SWR-PROTO-0007
title: Data is written to the field the specification names
type: software
status: reviewed
priority: medium
rationale: >
  Rule 2 and rule 3 of the format: text goes to text_data and binary to binary_data. The Java
  SDK follows this; the Go SDK writes binary_data for everything but application/protobuf
  (docs/spikes/protobuf.md, section 3). Owner decision 2026-10-24: write text_data for string
  payloads. A payload kept as JSON text is text. The proto_data rule is an open question of
  CR-0009 and is not stated here.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf_format` encoder shall write a `std::string` payload and a JSON payload
(`json_text` or `json_document`) to text_data, a `binary` payload to binary_data, and no data
field for an event without a payload.
