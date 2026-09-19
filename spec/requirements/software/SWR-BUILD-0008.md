---
uid: SWR-BUILD-0008
title: Headers free of anonymous-namespace entities
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 7 keeps the headers module-ready, and an entity with internal
  linkage in a header gives every translation unit its own copy, which becomes a
  hard error once the headers are consumed through a module interface.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/config_test.cpp::config-no-anonymous-namespace]
owner: filip.sajdak
version: 1
---
The installed headers shall contain no unnamed-namespace entity and no entity
declared `static` at namespace scope.
