---
uid: SWR-ADOPT-0009
title: CI proves the vcpkg port's released-source mode once the release it names exists
type: software
status: approved
priority: medium
rationale: >
  The vcpkg port has two sources (SWR-ADOPT-0006): the working tree, which CI builds, and the archive of the release tag, which a consumer gets and which is checked against a SHA-512.
  The archive's hash cannot be committed before the tag exists (SWR-ADOPT-0008), so the working-tree mode is the only one the packaging workflow can exercise on a pull request, and until now the released-source mode was proved by hand, once, after a release.
  A hash committed after the tag is exactly the change nothing else checks, and a wrong one makes every consumer's install fail.
  A job on main runs when the port changes, installs the port with no working-tree variable set, and builds test/consumer against what vcpkg installed, with the default features and with the rapidjson codec.
  Between the version bump and the post-tag commit the port names a tag that does not exist, or a hash that is still the previous release's, and a job that ran then would be red on the release pull request for a reason that is not a defect; a guard reads the tags on origin and the hashes committed at the tags, and skips with a notice in those two states.
  A job that only ever succeeds proves nothing, so the same run also offers vcpkg a copy of the port with a wrong hash and fails if vcpkg accepts it, and the hash vcpkg reports as the archive's actual hash must equal the one committed.
  The job reaches two third-party services, so it is not a required check, as the packaging workflow is not (SWR-ADOPT-0006).
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: []
verified_by: [test:scripts/test_vcpkg_release_guard.py::Decide, test:scripts/test_vcpkg_release_guard.py::Parse]
owner: filip.sajdak
version: 1
---
When the vcpkg port changes on main, the released-source job shall skip with a notice if the tag named by the port's version does not exist on origin or the committed SHA-512 is the one held at that tag's or the previous tag's port, and otherwise shall install the port without a working-tree source, build and run `test/consumer` against the installed result, and fail if vcpkg accepts a copy of the port carrying a wrong SHA-512.
