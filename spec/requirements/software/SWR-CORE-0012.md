---
uid: SWR-CORE-0012
title: data_t variant covering absent, textual, binary and JSON payloads
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 defines `data_t` so an event can carry no payload, text, opaque bytes
  or JSON, matching the payload cases the JSON format and HTTP binding must
  distinguish. CR-0003 adds `json_document`, so a decoded payload keeps the DOM
  its decoder built instead of being serialised back to text.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-data-t-variant]
owner: filip.sajdak
version: 2
---
The core header shall define `data_t` as a variant whose alternatives are
exactly `std::monostate`, `std::string`, `binary`, `json_text` and `json_document`.
