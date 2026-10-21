---
uid: SWR-ADOPT-0007
title: The repository ships a Conan recipe that a consumer builds against in CI
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  A project that manages its dependencies with Conan has the same need as one using vcpkg (SWR-ADOPT-0006), and SPEC section 9 deferred the recipe as well (D7).
  The owner decided on 2026-10-21 that the recipe lives in this repository and is consumed by `conan create` from a clone; submission to ConanCenter is a separate, later decision (D-ADOPT-2).
  The recipe reads its version from the project's CMakeLists.txt, so a release changes one number.
  Its package id keeps the codec options, because the codec choice is written into the installed package configuration, and a package built for nlohmann must not stand in for one built for RapidJSON.
  The recipe's test_package builds test/consumer against the package, so the consumer that proves the CMake install proves the Conan one too.
  CTRE and the codec libraries come from Conan; the boost_json option needs a compiled Boost.JSON, which CI does not build, and the recipe declares the option and says so.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:packaging/conan/conanfile.py, code:packaging/conan/test_package/conanfile.py, code:.github/workflows/packaging.yml]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-roundtrip, test:test/consumer/CMakeLists.txt::consumer-codec-link]
owner: filip.sajdak
version: 1
---
The repository shall ship the Conan recipe `packaging/conan/conanfile.py`, which installs the headers, the CMake package configuration and one option per codec that requires only that codec's dependency, and a CI job shall run `conan create` on it so that its `test_package` builds and runs `test/consumer` against the package using only what Conan installed.
