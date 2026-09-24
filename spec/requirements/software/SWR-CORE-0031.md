---
uid: SWR-CORE-0031
title: json_document holds a codec's DOM behind a codec-free type
type: software
status: approved
priority: high
rationale: >
  CR-0003 lets an event keep the JSON document its decoder built.
  A typed read or re-encode with the same codec then neither serialises nor parses the payload again.
  SWR-CORE-0013 still keeps any codec type out of the core header, so the DOM sits behind type erasure.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/json_document_test.cpp::json-document-holds-any-codec]
owner: filip.sajdak
version: 1
---
The core header shall define a `json_document` type that owns a parsed JSON document built by any codec while naming no codec or JSON library type.
