---
uid: SWR-JSON-0036
title: A codec header names only its own JSON library
type: software
status: approved
priority: high
rationale: >
  SPEC 4 fixes the dependency direction, and ADR-0004 makes the codec the only place a JSON library may appear. A codec header that reached for a second library would compile on any machine that has both installed and fail only in a job that deliberately installs one, so the check reads the headers rather than waiting for a build to notice.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/codec/rapidjson.hpp]
verified_by: [test:test/json_codec_test.cpp::codec-headers-are-mutually-isolated]
owner: filip.sajdak
version: 1
---
When the SDK ships more than one codec, each codec header shall name its own JSON
library and no other, and no header outside `include/cloudevents/codec/` shall name
any of them. Comments are excluded from this rule, because a doc comment
describing how another codec behaves is not a dependency.
