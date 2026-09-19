---
uid: SWR-CORE-0013
title: json_text holds serialized JSON so core stays codec-free
type: software
status: approved
priority: high
rationale: >
  SPEC 4 forbids core from depending on a format or codec header, so SPEC 5.1
  carries JSON payloads as already-serialized text rather than a codec DOM type,
  keeping the `CE_DEFAULT_CODEC=OFF` build free of nlohmann.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-json-text-codec-free]
owner: filip.sajdak
version: 1
---
The `json_text` type shall store serialized JSON as text, leaving the core header
free of any codec or JSON library type.
