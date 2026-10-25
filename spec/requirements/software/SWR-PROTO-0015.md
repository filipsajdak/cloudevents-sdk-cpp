---
uid: SWR-PROTO-0015
title: The Protobuf format is exported by the module in v4 only
type: software
status: reviewed
priority: medium
rationale: >
  SWR-BUILD-0010 ships a module wrapper that exports the public headers. The format is an
  addition to ce::v4 and is not declared in v3, v2 or v1.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/cloudevents.cppm]
verified_by: [test:test/module_test.cpp::module-exports-protobuf-format]
owner: filip.sajdak
version: 1
---
The `cloudevents.cppm` module shall export `protobuf_format` and `protobuf::decode_options` from
`ce::v4`, and no declaration of them shall appear in `ce::v3`, `ce::v2` or `ce::v1`.
