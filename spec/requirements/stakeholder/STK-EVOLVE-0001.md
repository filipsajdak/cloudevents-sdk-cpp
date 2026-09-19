---
uid: STK-EVOLVE-0001
title: Newer C++ standards are a recompile, not a port
type: stakeholder
status: approved
priority: high
rationale: >
  The SDK is expected to outlive several C++ standard revisions. Consumers must be
  able to raise their language level and gain the newer implementation without
  editing call sites, and the maintainers must be able to delete a polyfill once the
  standard provides it.
verification_method: test
security_classification: operational
derived_from: []
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::describe-backend-parity]
owner: filip.sajdak
version: 1
---
When a consuming project raises its C++ language level, the SDK shall present an
unchanged public interface so that the project gains the newer implementation by
recompiling rather than by changing its source.
