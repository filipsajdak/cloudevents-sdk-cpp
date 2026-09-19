---
uid: STK-ADOPT-0001
title: Adoptable by an existing C++20 codebase without imposing dependencies
type: stakeholder
status: approved
priority: high
rationale: >
  The first consumers are existing C++20 services that already pin their own JSON
  library, compile without exceptions, and cannot absorb a new transitive dependency
  tree or a new error-handling idiom. An SDK that forces any of those choices would
  be rejected regardless of its correctness.
verification_method: test
security_classification: operational
derived_from: []
satisfied_by: []
verified_by: [test:test/consumer/CMakeLists.txt::consumer-find-package]
owner: filip.sajdak
version: 1
---
The SDK shall be usable by a C++20 project that compiles without exceptions and that
supplies its own JSON library, without that project taking on any dependency it has
not chosen.
