---
uid: SWR-BUILD-0016
title: The release that delivered a requirement is derived from the tags
type: software
status: implemented
delivered_in: v0.6.1
priority: low
rationale: >
  A requirement is delivered by the first release whose tree holds its current statement and the tests that verify it.
  The tag that first contains the requirement's file is the wrong answer, because the spec is written before the code and the file exists for releases that did not yet deliver it.
  The code it names is not a condition, because a header that moved after the release would delay the tag to a release that delivered nothing new, and a test that resolves compiled against the code of its own tag.
  A requirement amended after its release is a different requirement from the one that shipped, so the older tag must not count for the new statement.
  Reading the tags with a tool, instead of by hand, is what keeps the traceability matrix from reporting shipped work as undelivered, and a check mode lets CI refuse a tree whose statuses have drifted from the tags.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:spec/tools/derive_delivered_in.py]
verified_by: [test:scripts/test_derive_delivered_in.py::FirstTagThatHoldsEverything, test:scripts/test_derive_delivered_in.py::NoTagQualifies, test:scripts/test_derive_delivered_in.py::WriteMode, test:scripts/test_derive_delivered_in.py::CheckMode]
owner: filip.sajdak
version: 1
---
When the current statement, version and verified_by tests of a requirement are all present in a release tag, the delivery tool shall report the first such tag as the release that delivered it, and shall report none for a requirement that no tag satisfies.
