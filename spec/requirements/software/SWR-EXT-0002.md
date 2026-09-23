---
uid: SWR-EXT-0002
title: Typed extension read and write through the describe seam
type: software
status: approved
priority: high
rationale: >
  SPEC 5.5 requires event::get<Ext>() and event::set(const Ext&) to map struct fields
  to and from extension attributes using the describe seam, so that adding an
  extension needs no new mapping code.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/extensions_test.cpp::extensions-get-set-roundtrip]
owner: filip.sajdak
version: 1
---
The SDK shall map the fields of a described extension struct to and from the event's
extension attributes, reporting a typed error when a required field is absent.
