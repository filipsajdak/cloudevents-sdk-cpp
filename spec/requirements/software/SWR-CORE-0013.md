---
uid: SWR-CORE-0013
title: A caller-supplied JSON payload is carried as text, and core names no codec type
type: software
status: implemented
delivered_in: v0.5.0
priority: high
rationale: >
  SPEC 4 forbids core from depending on a format or codec header, which keeps the
  `CE_DEFAULT_CODEC=OFF` build free of nlohmann. A caller who hands the SDK JSON
  hands it text, so `json_text` carries it as serialized JSON. CR-0003 lets a
  decoded payload keep its DOM as `json_document`, which holds the DOM behind
  type erasure, so the core header still names no codec type.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp, code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-json-text-codec-free]
owner: filip.sajdak
version: 2
---
The `json_text` type shall carry a caller-supplied JSON payload as serialized text,
and the core header shall name no codec or JSON library type.
