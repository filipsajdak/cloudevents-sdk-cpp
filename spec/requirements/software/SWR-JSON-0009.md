---
uid: SWR-JSON-0009
title: test::mini_codec as a second in-tree codec
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.3 requires a second, tiny in-tree codec used only by tests, because a concept exercised by a single implementation is a concept shaped around that implementation.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::mini-codec-satisfies-json-codec-concept]
owner: filip.sajdak
version: 1
---
The test tree shall provide test::mini_codec, an implementation that satisfies the json_codec concept independently of nlohmann and that the json_format tests instantiate alongside nlohmann_codec.
