---
uid: SWR-PROTO-0004
title: Required attributes are fields and every other attribute is a map entry
type: software
status: reviewed
priority: medium
rationale: >
  Rule 5 of the format: the four required attributes are explicit fields and optional and
  extension attributes are map entries keyed by name, datacontenttype, dataschema, subject and
  time included.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_format_test.cpp::protobuf-field-numbers-match-the-proto]
owner: filip.sajdak
version: 1
---
The `protobuf_format` encoder shall write id, source, specversion and type to the fields 1 to 4,
and write datacontenttype, dataschema, subject, time and every extension attribute as one entry
of the attributes map keyed by the attribute name, and the decoder shall read the same.
