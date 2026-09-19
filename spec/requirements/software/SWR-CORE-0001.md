---
uid: SWR-CORE-0001
title: Typed error code enumeration and error value
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 makes every failure path in the SDK report a machine-readable code
  rather than a thrown exception, so callers can branch on the failure kind while
  still receiving a human-readable detail string for diagnostics.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/result.hpp]
verified_by: [test:test/core_test.cpp::core-error-type]
owner: filip.sajdak
version: 1
---
The core header shall define an `errc` enumeration of failure codes and an
aggregate `error` holding an `errc code` member and a `std::string detail`
member.
