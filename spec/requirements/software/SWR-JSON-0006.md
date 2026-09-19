---
uid: SWR-JSON-0006
title: Codec kind inspection and checked accessors
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 requires kind inspection plus as_ accessors returning a result, so a decoder reading untrusted JSON discovers a type mismatch as a returned error instead of undefined behaviour.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp]
verified_by: [test:test/json_codec_test.cpp::json-codec-kind-and-as-accessors]
owner: filip.sajdak
version: 1
---
The json_codec concept shall demand kind inspection operations reporting whether a value is null, bool, number, string, array or object, and as_ accessors that return a result carrying either the extracted C++ value or an error when the value holds another kind.
