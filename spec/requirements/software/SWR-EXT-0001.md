---
uid: SWR-EXT-0001
title: One described struct per documented CloudEvents extension
type: software
status: approved
priority: high
rationale: >
  SPEC 5.5 requires the five documented extensions to be reachable as typed structs
  rather than as loose string attributes, so that a consumer names a field instead of
  a wire key and gets a compile error when the field does not exist.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/extensions.hpp]
verified_by: [test:test/extensions_test.cpp::extensions-described-structs]
owner: filip.sajdak
version: 1
---
The SDK shall provide one described struct for each of the distributed tracing,
partitioning, sequence, sampled rate and dataref extensions documented by the
CloudEvents specification.
