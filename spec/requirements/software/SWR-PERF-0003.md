---
uid: SWR-PERF-0003
title: A pull request that makes a decoded event retain more than 1 percent more memory fails
type: software
status: approved
priority: high
rationale: >
  An event kept in a queue or a cache holds its memory for as long as it lives.
  CR-0003 lets an event keep a parsed document, which costs more than its text, so the bytes a decoded event retains are tracked per payload size.
  The measurement is exact; the 1 percent allowance absorbs a standard library changing its allocation rounding.
  A pull request that trades this measure on purpose and stays within its budget carries the `perf: accepted` label instead, because raising a budget only to signal acceptance would not be a real limit. Anyone with triage rights on the repository can add it (ADR-0011); the growth is then listed as accepted by the label, and the budget in SWR-PERF-0004 still gates.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/compare.py, code:bench/perf/perf_probe.cpp, code:bench/perf/heap_counters.cpp, code:bench/perf/raw_malloc_wrapped.cpp, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::RetainedGate, test:test/malloc_accounting_test.cpp::malloc-accounting-counts-requested-bytes]
owner: filip.sajdak
version: 2
---
When a pull request increases the heap memory that a decoded event retains by more than 1 percent over main, the performance job shall fail, unless the pull request carries the `perf: accepted` label.
