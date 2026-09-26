---
uid: SWR-PERF-0007
title: Every change to main is recorded in the benchmark history
type: software
status: implemented
delivered_in: v0.5.0
priority: medium
rationale: >
  A comparison against main shows one step; a trend shows where a budget is heading and when a change began to cost.
  The history lives in the repository, on an orphan `bench-data` branch, so it needs no external service and survives workflow artifact expiry.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/history.py, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::History]
owner: filip.sajdak
version: 1
---
When a change merges into main, the performance job shall append its measurements to the `bench-data` branch and regenerate the trend report there.
