---
uid: SWR-BUILD-0015
title: A pull request that removes or changes a frozen declaration fails CI
type: software
status: approved
priority: high
rationale: >
  SWR-BUILD-0006 forbids changing a published declaration, and the release checklist asked a person to diff the headers against the previous tag by hand.
  A hand diff is a step nobody runs on the pull request that makes the change, so the first reader of a break is the consumer who upgrades.
  Each generation is frozen at the release that last published it as the current one: ce::v1 at what v0.3.0 published, ce::v2 at v0.4.0, ce::v3 at v0.5.0 and ce::v4 at v0.6.0.
  Entities unchanged since an earlier tag are declared once and brought into later generations, so the frozen declarations sit in the shared headers as well as under include/cloudevents/vN/.
  The check compares declarations and not text, because SWR-BUILD-0011 lets a defect fix change a body and a comment, and a text diff would refuse every such fix.
  An addition is allowed: SWR-BUILD-0006 permits adding to a generation, and an addition removes nothing a consumer compiled against.
  The check runs on the pull request, not at release, so the author learns of the break while the change is still theirs to move into a new generation.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:scripts/frozen_api_diff.py, code:.github/workflows/frozen-api.yml]
verified_by: [test:scripts/test_frozen_api_diff.py::Verdicts, test:scripts/test_frozen_api_diff.py::AgainstReleases]
owner: filip.sajdak
version: 1
---
When a pull request removes a declaration from a frozen generation (ce::v1, ce::v2, ce::v3, and ce::v4 from v0.6.0) or changes one, compared with the latest release tag, CI shall fail the pull request and list the declaration; a pull request that only adds declarations or changes function bodies shall pass.
