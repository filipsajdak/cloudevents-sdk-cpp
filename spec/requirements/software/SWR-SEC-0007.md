---
uid: SWR-SEC-0007
title: Line and branch coverage floors for core, format and binding
type: software
status: approved
priority: medium
rationale: >
  SPEC 6 sets a ninety percent line coverage target for the three layers that carry
  the specification logic. The figure is a floor that makes an untested branch
  visible in review rather than a goal in itself.
  A line can run with one arm of its decision never taken, so a line floor alone leaves
  an untested error return invisible. The branch floor counts decisions, with the
  exception edges of allocating calls excluded because the library throws nothing and
  no test can take them (CR-0007, D-SEC-5).
verification_method: test
security_classification: operational
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/CMakeLists.txt::coverage-report]
owner: filip.sajdak
version: 2
---
The SDK shall report line and branch coverage for its core, format and binding sources.
Line coverage shall reach ninety percent, and branch coverage with the compiler's
exception-handling edges excluded shall reach the floor `CE_COVERAGE_BRANCH_FLOOR`, before
a release is tagged. Both shall be enforced by the exit code of the coverage report and not
by reading its output. The branch floor is valid only for the pinned `gcovr` and `gcov` of
the `coverage floor` CI job, and is raised in the pull request that raises the measurement.
