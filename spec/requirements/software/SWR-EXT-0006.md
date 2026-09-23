---
uid: SWR-EXT-0006
title: Typed payload accessors live outside the core target
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.3 permits the typed payload accessors to be free functions in the format
  layer where that keeps core clean. Core may not name a codec, so placing them in
  core would break the downward dependency direction SPEC 4 fixes.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/typed_payload_test.cpp::typed-payload-layering]
owner: filip.sajdak
version: 1
---
The SDK shall declare the typed payload accessors in the format layer rather than in
the core target, so that the core headers name no codec type.
