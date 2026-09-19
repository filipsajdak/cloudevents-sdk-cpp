---
uid: SWR-DESC-0010
title: Unsupported member type diagnosed at compile time
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 requires a member outside the supported list to stop the build with a
  readable message, because the alternative is a template instantiation backtrace in
  which the offending field and the reason are both buried.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::unsupported-member-type-static-assert]
owner: filip.sajdak
version: 1
---
If a described member has a type outside the supported list, the reflection seam shall
fail the compilation through a `static_assert` whose message names the unsupported type.
