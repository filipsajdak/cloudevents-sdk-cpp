---
uid: SWR-JSON-0007
title: nlohmann_codec parses without exceptions
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 pins the nlohmann parse to allow_exceptions=false and maps failure to errc::parse_error, which keeps the codec usable in the -fno-exceptions builds that decision D4 keeps open.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::nlohmann-codec-non-throwing-parse]
owner: filip.sajdak
version: 1
---
When nlohmann_codec parses malformed JSON text, it shall report the failure as a result carrying errc::parse_error without throwing an exception.
