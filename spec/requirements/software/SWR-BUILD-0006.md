---
uid: SWR-BUILD-0006
title: Breaking API changes introduce a new version namespace
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 4 forbids mutating `v1` once published, because a source-breaking
  change under an unchanged namespace name leaves consumers with no way to pin
  the generation they compiled against.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/build_test.cpp::config-v1-immutable, test:test/v1/v1_generation_test.cpp::v1-declarations-survive, test:test/v2/v2_generation_test.cpp::v2-declarations-survive]
owner: filip.sajdak
version: 2
---
When a change breaks the published API, the changed entities shall be declared
in a new version namespace while the entities of every earlier generation keep
their existing declarations.
