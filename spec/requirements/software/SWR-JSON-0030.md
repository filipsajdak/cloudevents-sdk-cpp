---
uid: SWR-JSON-0030
title: Building without the default codec pulls in no nlohmann
type: software
status: approved
priority: high
rationale: >
  SPEC 4 and SPEC 5.3 make the nlohmann codec an opt-out build component, so a consuming project supplying its own codec takes on no nlohmann dependency.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:cmake/CeDependencies.cmake]
verified_by: [test:test/json_codec_test.cpp::build-without-default-codec-has-no-nlohmann]
owner: filip.sajdak
version: 1
---
When the build is configured with CE_DEFAULT_CODEC set to OFF, the resulting library and its installed headers shall reference no nlohmann header, symbol or package.
