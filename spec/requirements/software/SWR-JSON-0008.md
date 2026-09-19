---
uid: SWR-JSON-0008
title: nlohmann types confined to the nlohmann codec header
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 states that no nlohmann type appears in any other header, which is what lets a consuming project supply its own codec without linking nlohmann at all.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/codec/nlohmann.hpp]
verified_by: [test:test/json_codec_test.cpp::no-nlohmann-type-outside-codec-header]
owner: filip.sajdak
version: 1
---
The public headers of the SDK other than the nlohmann codec header shall name no nlohmann type in any declaration, definition or include directive.
