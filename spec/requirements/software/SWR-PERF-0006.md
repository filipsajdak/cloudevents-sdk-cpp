---
uid: SWR-PERF-0006
title: A binary that grows more than 5 percent is flagged
type: software
status: reviewed
priority: low
rationale: >
  The SDK is header-only, so its code lands in every consumer binary.
  Binary size depends on the compiler as much as on the change, so growth warns rather than fails, and the warning is visible in the pull request.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: []
verified_by: [test:bench/perf/test_perf_tools.py::BinarySizeWarning]
owner: filip.sajdak
version: 1
---
When a pull request increases the size of a consumer binary by more than 5 percent over main, the performance job shall report a warning on the pull request.
