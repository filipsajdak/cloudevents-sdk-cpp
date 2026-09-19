---
uid: SWR-ADOPT-0004
title: Installed package is consumable through find_package
type: software
status: approved
priority: high
rationale: >
  SPEC 7 milestone M0 makes a consumer project the acceptance test for packaging.
  A header-only library that cannot be installed and found is one a downstream build
  has to vendor by hand, which defeats the distribution goal.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:cmake/cloudeventsConfig.cmake.in, code:CMakeLists.txt]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-find-package]
owner: filip.sajdak
version: 1
---
When the SDK has been installed, a separate CMake project shall locate it with
find_package and compile against its targets without naming any include path
explicitly.
