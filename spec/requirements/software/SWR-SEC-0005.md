---
uid: SWR-SEC-0005
title: Interoperability goldens produced by the Go and Java SDKs
type: software
status: approved
priority: high
rationale: >
  SPEC 6 makes cross-SDK goldens the external correctness measure, because this SDK
  has no reference C++ peer to agree with. Committing the goldens rather than the
  SDKs keeps the suite runnable without a Go or Java toolchain present.
verification_method: test
security_classification: operational
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/interop_test.cpp::interop-golden-corpus, test:test/interop_test.cpp::interop-behaviour-audit]
owner: filip.sajdak
version: 1
---
The SDK shall decode golden JSON produced by the Go and Java CloudEvents SDKs, and
shall be committed together with the generator sources that produced those goldens.
