---
uid: SWR-PROTO-0003
title: Field numbers follow cloudevents.proto at v1.0.2
type: software
status: reviewed
priority: high
rationale: >
  The prose of protobuf-format.md numbers the attribute map 1 and the data fields 2 to 4, and
  cloudevents.proto at v1.0.2 numbers them 5 and 6 to 8. Both the Go and the Java SDK implement
  the .proto, and the spike (pull request 113) decoded their bytes against it with protoc. The
  wire follows the .proto (CLAUDE.md rule 5: the discrepancy is reported, not picked silently;
  owner decision 2026-10-24, ADR-0015).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_wire_test.cpp::protobuf-wire-writer, test:test/protobuf_format_test.cpp::protobuf-field-numbers-match-the-proto]
owner: filip.sajdak
version: 1
---
The `protobuf_format` encoder and decoder shall use the field numbers of cloudevents.proto at
the tag v1.0.2: id 1, source 2, spec_version 3, type 4, attributes 5, binary_data 6, text_data 7
and proto_data 8, and, in an attribute value, ce_boolean 1, ce_integer 2, ce_string 3, ce_bytes
4, ce_uri 5, ce_uri_ref 6 and ce_timestamp 7.
