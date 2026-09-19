---
uid: SWR-CORE-0012
title: data_t variant covering absent, textual, binary and JSON payloads
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 defines `data_t` so an event can carry no payload, text, opaque bytes
  or JSON, matching the payload cases the JSON format and HTTP binding must
  distinguish.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-data-t-variant]
owner: filip.sajdak
version: 1
---
The core header shall define `data_t` as a variant whose alternatives are
exactly `std::monostate`, `std::string`, `binary` and `json_text`.
