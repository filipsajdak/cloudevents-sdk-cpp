---
uid: STK-PERF-0001
title: A change that makes event handling costlier is caught before it merges
type: stakeholder
status: approved
priority: high
rationale: >
  The SDK sits on the hot path of every service that sends or receives an event.
  A consumer cannot see a slowdown or a new allocation in a header-only library until it shows up in their own production latency or memory.
  The maintainers therefore need each pull request's cost measured against the one before it, and against limits they have set.
verification_method: demonstration
security_classification: operational
derived_from: []
satisfied_by: [code:.github/workflows/perf.yml, doc:docs/PERFORMANCE.md]
verified_by: [test:bench/perf/test_perf_tools.py::SummaryTable, test:bench/perf/test_perf_tools.py::InstructionGate, test:bench/perf/test_perf_tools.py::AllocationGate]
owner: filip.sajdak
version: 1
---
When a pull request changes how much time or memory an event operation costs, the SDK maintainers shall learn of the change before the pull request merges.
