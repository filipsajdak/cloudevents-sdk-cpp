---
uid: SWR-SEC-0002
title: Address and undefined-behaviour sanitizers run the whole suite
type: software
status: approved
priority: high
rationale: >
  SPEC 6 requires a sanitizer job over the full suite. Fuzzing explores inputs the
  fixtures do not cover, but only the sanitizers turn a latent memory error on a
  known-good input into a visible failure.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/CMakeLists.txt::asan-ubsan-suite]
owner: filip.sajdak
version: 1
---
The SDK shall run its complete test suite under the address and undefined-behaviour
sanitizers as a continuous integration job that fails the build on any report.
