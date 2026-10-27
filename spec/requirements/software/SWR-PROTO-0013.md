---
uid: SWR-PROTO-0013
title: The Protobuf format includes no third-party header
type: software
status: approved
priority: medium
rationale: >
  Owner decision 2026-10-24: the codec is hand-written, with no libprotobuf and no generated
  code, and the core stays CTRE-only. The spike's prototype measured 455 formatted lines and
  decoded both SDKs' bytes. No dependency wording of CR-0006 changes.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp, code:include/cloudevents/format/detail/protobuf_wire.hpp]
verified_by: [test:test/protobuf_format_test.cpp::protobuf-format-includes-no-third-party-header]
owner: filip.sajdak
version: 1
---
The header of `protobuf_format` shall include only the standard library and headers of
`ce::core`, and shall not require protoc, a generated file or a compiled library.
