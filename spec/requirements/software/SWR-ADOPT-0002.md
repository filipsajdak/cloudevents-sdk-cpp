---
uid: SWR-ADOPT-0002
title: The core depends on no third-party library except CTRE
type: software
status: approved
priority: high
rationale: >
  A consumer that already vendors its own JSON library cannot absorb a second one.
  Confining every third-party include to the codec target is what makes the core
  adoptable, and CTRE is the single exception because the specification forbids
  std::regex for attribute parsing.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:CMakeLists.txt, code:cmake/CeDependencies.cmake]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-no-nlohmann]
owner: filip.sajdak
version: 1
---
When the build is configured with the default codec disabled, the SDK shall compile
its core, format and binding targets without including any third-party header other
than CTRE.
