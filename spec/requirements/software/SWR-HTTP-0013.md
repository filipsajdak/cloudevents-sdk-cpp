---
uid: SWR-HTTP-0013
title: Binary-mode extension values decode as strings
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.4 notes that a binary-mode header carries no type information, so the receiver
  cannot recover whether an extension was a boolean, an integer or text; decoding to
  `std::string` keeps the value lossless and leaves typing to the typed extension
  structs of SPEC 5.5.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::binary-mode-extension-string-type]
owner: filip.sajdak
version: 1
---
When decoding a `message` in binary mode, the binding shall store every extension
attribute value as a `std::string`.
