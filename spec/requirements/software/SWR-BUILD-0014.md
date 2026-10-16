---
uid: SWR-BUILD-0014
title: A pull request shows which changed lines no suite pins, and is never blocked by it
type: software
status: approved
priority: low
rationale: >
  A line can be covered and still unpinned: a suite runs it and no assertion notices when its operator flips.
  Mutation testing on the lines a pull request changes shows those lines, and CONTRIBUTING.md asks every author to run it and end the Verification with its one-line result.
  Running it on the pull request itself gives the reviewer the same line from a machine that is not the author's, with the surviving mutants listed beside it.
  Mutation scores depend on the operators and on the clang that built the suites, so a surviving mutant is a question for the author and the reviewer, not a failure.
  The job therefore reports and never gates, as the performance job's wall time and binary size never gate (SWR-PERF-0006), and it stays out of the required checks.
  The tool failing to run is a different matter: a job that went green without having mutated anything would read as a clean result, so that outcome fails the job.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:.github/workflows/mutation.yml, code:scripts/mutation_report.py, code:scripts/mutation.sh]
verified_by: [test:scripts/test_mutation_report.py::ReportBody]
owner: filip.sajdak
version: 1
---
When a pull request changes lines under include/cloudevents/, the mutation job shall publish the `Mutation:` verification line and every surviving mutant in the job summary and in one pull request comment, and shall not fail the job because a mutant survived.
