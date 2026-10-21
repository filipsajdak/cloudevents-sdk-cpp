---
uid: SWR-ADOPT-0008
title: A packaging recipe whose version differs from the project's version fails a check
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  The vcpkg port names a release by version and by the SHA-512 of that release's source archive, and the Conan recipe by version; each has to move with the tag (docs/RELEASE.md).
  A recipe left at the previous version installs the previous release under the new name, and nothing in a build of the working tree shows it, because CI builds the recipes from the working tree.
  A script compares the version in the vcpkg manifest and the version the Conan recipe reads with the project VERSION in CMakeLists.txt, and the same script runs as a test in the suite, so a version bump that forgets a recipe fails the pull request that makes it.
  The SHA-512 cannot be checked at that point, because the archive exists only once the tag does; the release checklist updates it after tagging.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:scripts/check_packaging_version.py]
verified_by: [test:scripts/test_packaging_version.py::PackagingVersion]
owner: filip.sajdak
version: 1
---
The packaging version check shall fail, naming the file, when the version in `packaging/vcpkg/ports/cloudevents-cpp/vcpkg.json` differs from the project VERSION in `CMakeLists.txt`, or when `packaging/conan/conanfile.py` does not read its version from that file.
