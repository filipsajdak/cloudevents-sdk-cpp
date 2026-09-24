---
uid: SWR-PERF-0004
title: A measurement above its committed budget fails
type: software
status: approved
priority: high
rationale: >
  A threshold against main catches a large step but not a drift of many small ones.
  Budgets in `bench/budgets.json` set an absolute limit per operation, codec and measure.
  They start at the measurements of main plus 10 percent headroom, and change only by an explicit edit that a reviewer sees.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/compare.py, code:bench/perf/seed_budgets.py, code:bench/budgets.json, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::BudgetGate, test:bench/perf/test_perf_tools.py::AcceptedCost, test:bench/perf/test_perf_tools.py::SeedBudgets]
owner: filip.sajdak
version: 1
---
If a measurement of instruction count, heap allocations or retained memory exceeds its limit in `bench/budgets.json`, then the performance job shall fail.
