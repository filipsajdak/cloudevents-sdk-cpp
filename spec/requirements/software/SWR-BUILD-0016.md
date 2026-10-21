---
uid: SWR-BUILD-0016
title: The release that delivered a requirement is derived from the tags
type: software
status: approved
priority: low
rationale: >
  A requirement is delivered by the first release whose tree holds its current statement and the tests that verify it.
  The tag that first contains the requirement's file is the wrong answer, because the spec is written before the code and the file exists for releases that did not yet deliver it.
  The code it names is not a condition, because a header that moved after the release would delay the tag to a release that delivered nothing new, and a test that resolves compiled against the code of its own tag.
  A requirement amended after its release is a different requirement from the one that shipped, so the older tag must not count for the new statement.
  Reading the tags with a tool, instead of by hand, is what keeps the traceability matrix from reporting shipped work as undelivered, and a check mode lets CI refuse a tree whose statuses have drifted from the tags.
  The check runs on the pull request, so a requirement a release delivered cannot stay `approved` without `delivered_in`: the author sees it while the change is theirs, not when the traceability matrix is read.
  A requirement no tag holds yet passes, because the tool cannot call it delivered: a new requirement with its tests, an amended one that returned to `approved` (D-EXT-6), and everything the release pull request carries before its tag exists.
  A check that cannot read the tags must fail and not pass, because a tree with no tags derives nothing and would look clean; CI therefore fetches the full history with its tags.
  Between tagging and the post-tag pull request the check fails on main for the requirements the tag delivered, by design (D-SPEC-5).
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:spec/tools/derive_delivered_in.py, code:.github/workflows/spec.yml]
verified_by: [test:scripts/test_derive_delivered_in.py::FirstTagThatHoldsEverything, test:scripts/test_derive_delivered_in.py::NoTagQualifies, test:scripts/test_derive_delivered_in.py::WriteMode, test:scripts/test_derive_delivered_in.py::CheckMode]
owner: filip.sajdak
version: 2
---
When the current statement, version and verified_by tests of a requirement are all present in a release tag, the delivery tool shall report the first such tag as the release that delivered it, and shall report none for a requirement that no tag satisfies; in check mode it shall fail, naming each requirement file and the tag that delivers it, when a requirement is `approved` though a tag delivers it, shall pass when no tag does, and shall fail with a distinct status when no release tag can be read.
