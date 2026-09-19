---
uid: SWR-SEC-0007
title: Line coverage floor for core, format and binding
type: software
status: approved
priority: medium
rationale: >
  SPEC 6 sets a ninety percent line coverage target for the three layers that carry
  the specification logic. The figure is a floor that makes an untested branch
  visible in review rather than a goal in itself.
verification_method: test
security_classification: operational
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/CMakeLists.txt::coverage-report]
owner: filip.sajdak
version: 1
---
The SDK shall report line coverage for its core, format and binding sources, and that
coverage shall reach ninety percent before a release is tagged.
