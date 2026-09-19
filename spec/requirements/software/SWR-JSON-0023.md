---
uid: SWR-JSON-0023
title: Documented extension type loss on decode
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 records that JSON loses the CloudEvents attribute type, fixes the mapping the decoder uses, and points at typed extension structs for recovering richer types.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::extension-decode-type-mapping]
owner: filip.sajdak
version: 1
---
The json_format decoder shall decode a JSON bool extension value to bool, an integral JSON number extension value to std::int32_t and a JSON string extension value to std::string, with that type loss stated in the public documentation of the decoder.
