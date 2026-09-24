---
uid: SWR-PERF-0002
title: A pull request that adds a heap allocation fails
type: software
status: reviewed
priority: high
rationale: >
  The number and size of allocations an operation makes are exact, so any increase is a real change in behaviour.
  An allocation added to a per-event path is paid on every event a service handles.
  A pull request that needs one says so by raising the budget in the same change, where a reviewer sees it.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: []
verified_by: [test:bench/perf/test_perf_tools.py::AllocationGate]
owner: filip.sajdak
version: 1
---
When a pull request increases the number or the total bytes of heap allocations of a benchmarked operation over main, the performance job shall fail.
