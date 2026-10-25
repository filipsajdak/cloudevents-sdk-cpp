---
uid: SWR-PROTO-0008
title: Data is read from any of the three data fields
type: software
status: approved
priority: medium
rationale: >
  The Go SDK writes binary_data where the Java SDK writes text_data for the same event, so a
  reader that wants both SDKs' events has to accept all three fields. The oneof semantic of
  Protobuf (the last field on the wire wins) decides a message that carries two. Ruled by the owner on 2026-10-25 (CR-0009, items 7, 9 and 13): an empty
  binary_data of an event with no datacontenttype is no payload, and a proto_data value is a binary payload whose type URL
  fills a missing dataschema. The JSON classification is the optional decode_as of SWR-PROTO-0025.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_data_test.cpp::protobuf-data-read-arms]
owner: filip.sajdak
version: 2
---
The `protobuf_format` decoder shall return the payload of text_data as a `std::string` holding
the bytes unchanged and the payload of binary_data as a `binary`, except that an empty binary_data in an event with no datacontenttype
is no payload, shall return the value of proto_data as a `binary` payload, set datacontenttype to application/protobuf
if it is absent and set dataschema to the type URL if it is absent, keeping dataschema when both are present, treating an empty type URL as absent and failing with `errc::parse_error` for a non-empty type URL that is not a valid URI-reference, and shall, for a message with more than one data field, use the one that appears
last.
