---
uid: SYS-ADOPT-0001
title: Header-only, exception-free, dependency-light distribution
type: system
status: approved
priority: high
rationale: >
  Consumers compile without exceptions and pin their own JSON library. A header-only
  distribution with a layered target graph lets a project take the core without the
  JSON codec, and an error-return discipline lets it build with exceptions disabled.
verification_method: test
security_classification: operational
derived_from: [STK-ADOPT-0001]
satisfied_by: []
verified_by: [test:test/consumer/CMakeLists.txt::consumer-find-package]
owner: filip.sajdak
version: 1
---
The SDK shall report every recoverable failure as a returned value rather than a
thrown exception, and shall build as header-only targets whose dependency direction
runs from bindings and formats towards the core and never the reverse.
