---
uid: SWR-PERF-0001
title: A pull request that adds more than 2 percent instructions fails
type: software
status: approved
priority: high
rationale: >
  The instruction count of an operation under Valgrind varies by well under 1 percent between runs on the same binary, so a 2 percent threshold is signal rather than noise.
  It is the gate that stands in for run time, which shared runners cannot measure reliably.
  A pull request that trades this measure on purpose and stays within its budget carries the `perf: accepted` label instead, because raising a budget only to signal acceptance would not be a real limit. Anyone with triage rights on the repository can add it (ADR-0011); the growth is then listed as accepted by the label, and the budget in SWR-PERF-0004 still gates.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/compare.py, code:bench/perf/perf_probe.cpp, code:bench/perf/CMakeLists.txt, code:bench/perf/measure.py, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::InstructionGate, test:bench/perf/test_perf_tools.py::LabelAcceptedRegression]
owner: filip.sajdak
version: 2
---
When a pull request increases the instruction count of a benchmarked operation by more than 2 percent over main, the performance job shall fail, unless the pull request carries the `perf: accepted` label.
