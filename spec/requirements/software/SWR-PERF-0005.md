---
uid: SWR-PERF-0005
title: Every pull request shows its measurements beside main and the budgets
type: software
status: approved
priority: medium
rationale: >
  A failed gate with no numbers sends the author off to reproduce the measurement.
  A table of every measurement for main, the pull request and the budget shows at once which operation moved and by how much, including the run time and binary size that never gate.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: [code:bench/perf/compare.py, code:.github/workflows/perf.yml]
verified_by: [test:bench/perf/test_perf_tools.py::SummaryTable]
owner: filip.sajdak
version: 1
---
The performance job shall publish, for every pull request, a table of each measurement for main, for the pull request and for its budget.
