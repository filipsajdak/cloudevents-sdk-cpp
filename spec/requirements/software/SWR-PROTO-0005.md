---
uid: SWR-PROTO-0005
title: Attribute values keep their CloudEvents type
type: software
status: reviewed
priority: medium
rationale: >
  Protobuf, unlike JSON, carries the attribute type (SWR-JSON-0023 describes the loss in JSON).
  The mapping is bool to ce_boolean, int32 to ce_integer, string to ce_string, binary to
  ce_bytes, URI to ce_uri, URI-reference to ce_uri_ref and timestamp to ce_timestamp. The Go SDK
  writes dataschema as ce_uri and drops a ce_uri_ref dataschema on read; the Java SDK writes
  ce_uri_ref for a relative one; so the reader accepts both arms for a URI-valued attribute.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf_format` encoder shall write each attribute value in the arm of its type and write
dataschema as ce_uri, and the decoder shall return an extension attribute in the type of the arm
it was read from, and shall accept ce_uri and ce_uri_ref for dataschema and for source.
