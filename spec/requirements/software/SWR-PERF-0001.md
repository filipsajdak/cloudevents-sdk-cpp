---
uid: SWR-PERF-0001
title: A pull request that adds more than 2 percent instructions fails
type: software
status: reviewed
priority: high
rationale: >
  The instruction count of an operation under Valgrind varies by well under 1 percent between runs on the same binary, so a 2 percent threshold is signal rather than noise.
  It is the gate that stands in for run time, which shared runners cannot measure reliably.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a pull request increases the instruction count of a benchmarked operation by more than 2 percent over main, the performance job shall fail.
