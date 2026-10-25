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
  payloads. A payload kept as JSON text is text. Ruled by the owner on 2026-10-25 (CR-0009, items 7 and 9):
  an empty binary is written as a binary_data of length 0, and proto_data is written only for a binary payload
  with the content type application/protobuf and a dataschema, whose value is the type URL.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_data_test.cpp::protobuf-data-write-arms]
owner: filip.sajdak
version: 2
---
The `protobuf_format` encoder shall write a `std::string` payload and a JSON payload
(`json_text` or `json_document`) to text_data, a `binary` payload to binary_data, and no data
field for an event without a payload, except that it shall write a `binary` payload whose datacontenttype is
application/protobuf and whose event has a dataschema to proto_data, with the dataschema, verbatim, as the type URL, and the dataschema also as its own attribute.
