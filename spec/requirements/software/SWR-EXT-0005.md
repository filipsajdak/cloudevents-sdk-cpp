---
uid: SWR-EXT-0005
title: Typed payload read and write for described types
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 names event::data_as<T>(codec) and event::set_data(const T&, codec) as the
  typed payload surface, and places them in the format layer so that core stays free
  of any codec type. They are the pairing of the describe seam with the JSON format.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/typed_payload_test.cpp::typed-payload-roundtrip]
owner: filip.sajdak
version: 1
---
The SDK shall serialize a described payload type into the event data and read it back
into that type through a caller-supplied codec, returning a typed error when the
stored data does not match the requested type.
