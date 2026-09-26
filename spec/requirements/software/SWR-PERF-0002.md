---
uid: SWR-PERF-0002
title: A pull request that adds a heap allocation fails
type: software
status: implemented
delivered_in: v0.5.0
priority: high
rationale: >
  The number and size of allocations an operation makes are exact, so any increase is a real change in behaviour.
  An allocation added to a per-event path is paid on every event a service handles.
  A pull request that needs one says so by raising the budget in the same change, where a reviewer sees it.
  A pull request that trades this measure on purpose and stays within its budget carries the `perf: accepted` label instead, because raising a budget only to signal acceptance would not be a real limit. Anyone with triage rights on the repository can add it (ADR-0011); the growth is then listed as accepted by the label, and the budget in SWR-PERF-0004 still gates.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/compare.py, code:bench/perf/counting_allocator.cpp, code:bench/perf/heap_counters.cpp, code:bench/perf/raw_malloc_wrapped.cpp, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::AllocationGate, test:bench/perf/test_perf_tools.py::LabelAcceptedRegression, test:test/malloc_accounting_test.cpp::malloc-accounting-counts-requested-bytes]
owner: filip.sajdak
version: 2
---
When a pull request increases the number or the total bytes of heap allocations of a benchmarked operation over main, the performance job shall fail, unless the pull request carries the `perf: accepted` label.
