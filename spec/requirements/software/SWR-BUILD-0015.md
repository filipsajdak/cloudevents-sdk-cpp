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
  The check verifies SWR-BUILD-0006 for declarations; it cannot see a change to documented behaviour, which stays with the reviewer.
  The owner ruled on 2026-10-21 (CR-0005) that a data member appended after the last one of a class, with a default member initialiser, is an addition: every initialiser a consumer wrote still compiles and reads the same.
  A default argument that value-initialises such a class (`= {}`) spells every member, so its syntax tree grows with the class, and it passes for the same reason: no call reads differently.
  A member appended without an initialiser, one inserted before another, a reordered, removed or retyped member, and a changed default are not.
  The check runs on the pull request, not at release, so the author learns of the break while the change is still theirs to move into a new generation.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:scripts/frozen_api_diff.py, code:.github/workflows/frozen-api.yml]
verified_by: [test:scripts/test_frozen_api_diff.py::Verdicts, test:scripts/test_frozen_api_diff.py::AgainstReleases]
owner: filip.sajdak
version: 3
---
When a pull request removes a declaration from a frozen generation (ce::v1, ce::v2, ce::v3, and ce::v4 from v0.6.0) or changes one, compared with the latest release tag, CI shall fail the pull request and list the declaration; a pull request that only adds declarations or changes function bodies, and one that appends a data member with a default member initialiser after the last member of a class, shall pass, as shall the default arguments that value-initialise that class with `{}`.
