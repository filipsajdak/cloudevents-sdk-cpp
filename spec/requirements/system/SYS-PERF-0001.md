---
uid: SYS-PERF-0001
title: Every pull request is measured against main and against budgets
type: system
status: reviewed
priority: high
rationale: >
  Shared CI runners vary by 10 to 20 percent in wall time between runs, so a gate on time alone would fail for noise.
  Instruction counts, heap allocations and retained bytes are deterministic, so they can gate.
  Wall time and binary size are still worth tracking, and are reported but never gate.
  Measuring main and the pull request in the same job cancels runner differences, and committed budgets catch a slow drift that no single pull request makes.
verification_method: demonstration
security_classification: operational
derived_from: [STK-PERF-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall measure, for every pull request, the instruction count, heap allocations, retained memory, run time and binary size of its event operations against the same measurements of main and against committed budgets.
