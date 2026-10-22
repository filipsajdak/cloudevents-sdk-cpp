# Release checklist

Everything here is a gate that a machine can run. A step that reads "check that
..." without a command is not a step.

A generation's API is fixed at the tag that published it: `ce::v1` is what
`v0.3.0` published, `ce::v2` what `v0.4.0` published, `ce::v3` what `v0.5.0`
published and `ce::v4` what `v0.6.0` published. `SWR-BUILD-0006` requires a new
version namespace for any breaking change after that, and the break bumps the
minor version before 1.0 (`D-BUILD-4`). A later tag may add to a generation and
may not remove from it, so anything that should go has to go before the *first*
tag of a namespace.

The version a tag takes follows from what changed since the previous one, until
1.0:

| Change since the previous tag | Version | Where it goes |
|---|---|---|
| A declaration removed or changed, or documented behaviour changed | minor | a new generation (`ce::vN+1`) |
| A declaration added, or a defect fixed with every declaration and documented behaviour kept | patch | the current generation, and every earlier one the fix applies to |

Run every command from a clean `main` at the commit being tagged.

## Before tagging

- [ ] `main` is green: every CI check, not only the compiler matrix.
      `gh pr checks` on the last merged pull request is the quickest reading.
- [ ] `python3 spec/tools/lint_requirements.py` exits 0.
- [ ] `python3 spec/tools/check_trace.py` exits 0.
- [ ] `python3 spec/tools/check_references.py` exits 0 with **no warnings**. A
      warning means a requirement names a test file nobody has written, which
      before a release is an unverified requirement rather than pending work.
- [ ] Every preset configures, builds and tests locally, judged by exit code at
      each stage rather than by reading ctest's summary.
- [ ] `cmake --build build --target asan-ubsan-suite` is clean (`SWR-SEC-0002`).
- [ ] `cmake --build build --target coverage-report` meets the floor
      (`SWR-SEC-0007`). It fails the build below it; do not lower
      `CE_COVERAGE_FLOOR` to make a release go out.
      **The `coverage floor` CI job is the measurement of record**, because
      the local toolchain has twice reported a number that was not the
      coverage. Apple's `gcov` cannot read GCC's `.gcda` and returns
      `0.0% (0 out of 0)`; passing `-DCE_GCOV_EXECUTABLE=gcov-<n>` fixes that
      and, measured on 2026-09-21 with g++-16 and gcov-16 on macOS, still
      under-reported at 27.4% while the CI job on g++-13 passed the floor on
      the same commit. Read a local number below the floor as a question
      about the toolchain until CI agrees with it.
- [ ] Each fuzz target has run ten minutes clean on the release commit
      (`SWR-SEC-0001`). Pull-request CI runs a one-minute budget, so the nightly
      `fuzz` workflow is the evidence, and it counts only if it ran on that
      commit. Run it on demand from the Actions tab if the nightly predates it.
- [ ] `./interop/run.sh` regenerates the goldens and both SDKs accept every
      document and every Kafka record this one produced (`SWR-SEC-0005`,
      `SWR-SEC-0008`).
- [ ] `git diff --exit-code test/fixtures/interop` is empty after that run, or
      the change is understood and described in the release notes.
- [ ] `cmake -S . -B build -DCE_BUILD_MODULE=ON` builds and `module_consumer`
      exits 0 on a toolchain that supports modules (`SWR-BUILD-0010`).
- [ ] The installed package is consumed from `test/consumer/` with the system
      copy of any dependency hidden, so a stale include path cannot mask a
      missing one.
- [ ] The `packaging` workflow is green on the release commit: the vcpkg port
      and the Conan recipe each built and ran `test/consumer`
      (`SWR-ADOPT-0006`, `SWR-ADOPT-0007`). Run it from the Actions tab if the
      last run predates the commit.
- [ ] `python3 scripts/check_packaging_version.py` exits 0: the `version` in
      `packaging/vcpkg/ports/cloudevents-cpp/vcpkg.json` is the new `VERSION`
      from `CMakeLists.txt`, and the Conan recipe still reads it from there
      (`SWR-ADOPT-0008`). Edit `vcpkg.json` in the same commit as the `VERSION`
      bump; the CTest `packaging_tools` fails the build if it is forgotten.

## The API is frozen at the tag

- [ ] `python3 scripts/frozen_api_diff.py --base <previous tag>` exits 0: no
      declaration of `ce::v1` to `ce::v4` was removed or changed since that tag
      (`SWR-BUILD-0006`, `SWR-BUILD-0015`). The `frozen-api` workflow runs it on
      every pull request against the latest tag, so this is the same check run
      once more on the commit being tagged. Exit 1 lists what moved; exit 2
      means it did not run, which is not a pass. It needs clang and the
      dependencies' headers (see `CONTRIBUTING.md`, "Frozen API").
- [ ] No documented behaviour changed in a generation a tag has published. The
      script sees declarations only, so read the diff of the guide and of the
      doc comments for a changed result, default or error (`D-BUILD-4`).
- [ ] No entity in the current generation is one you would rather remove. Check the `errc`
      enumerators in particular: a code nothing can produce is dead surface, and
      `SWR-SEC-0003` requires a negative test for every one of them.
- [ ] `test/build_test.cpp` pins the `errc` values. Confirm the pins match the
      enum, because they are the ABI promise from this tag onward.
- [ ] `CE_DESCRIBE` is still the only macro the headers leak
      (`SWR-BUILD-0009`).

## Tagging

- [ ] `docs/DECISIONS.md` has an entry for every judgement call made since the
      previous tag.
- [ ] Release notes name the measured facts, not the intentions: the coverage
      number, the fuzz durations, the toolchains actually tested.
- [ ] `CMakeLists.txt` `VERSION` matches the tag about to be created.
- [ ] `CHANGELOG.md` has a section for it, naming what changed rather than
      which pull requests changed it.
- [ ] Tag on `main`, signed.
- [ ] `python3 spec/tools/derive_delivered_in.py --write` run after the tag
      exists, and its edits committed: requirements the release delivered gain
      `delivered_in` (D-SPEC-4). The spec gate fails every pull request from the
      tag until these edits are on main (D-SPEC-5), so this is the first pull
      request after the tag, together with the vcpkg hash below, and it is merged
      before anything else. Update the pull requests that were red meanwhile.
- [ ] The `SHA512` in `packaging/vcpkg/ports/cloudevents-cpp/portfile.cmake` is
      the hash of the new tag's archive, committed after the tag exists (the
      archive cannot be hashed before it does):
      `curl -sL https://github.com/filipsajdak/cloudevents-sdk-cpp/archive/refs/tags/vX.Y.Z.tar.gz | shasum -a 512`.
      Until that commit lands, the released-source mode names the previous
      release's hash, which is why the `packaging` workflow builds the working
      tree instead. The `vcpkg-released` job on main proves the hash after the
      post-tag PR merges: it installs the port without `CLOUDEVENTS_CPP_SOURCE_DIR`,
      so vcpkg downloads the archive and refuses a mismatch, and it also
      checks that a wrong hash is refused (`SWR-ADOPT-0009`). It skips, with a
      notice, while the tag is missing or the hash is not yet new; read its
      result in the Actions tab, and dispatch it manually if the post-tag PR
      did not touch `packaging/vcpkg/`.
- [ ] The tag builds from a clean clone with no network access beyond the
      dependency fetch.

## Known limitations to state in the notes

These are measured and recorded in `docs/DECISIONS.md`. A release that does not
mention them will generate the same questions repeatedly.

- libc++ 17 and 18 implement `std::format` without defining
  `__cpp_lib_format`; the SDK carves them out by version (`D-CI-1`).
- Clang 16 cannot compile libstdc++ 13 or 14 in C++23 mode, so the C++23 Clang
  job uses libc++ (`D-CI-1`).
- The module interface is usable, with constraints on what an importing
  translation unit may also include (`D-MODULE-1`).
- Given the same non-JSON payload, the Java SDK writes `data_base64` where Go
  and this SDK write a JSON string. Both are legal (`D-INTEROP-1`).
- A binary-mode body under a JSON media type is parsed once on receive in
  `ce::v4`, and `retain_document_up_to = 0` does not skip the parse.
  `parse_binary_body = false` skips it, and then nothing validates the body. A
  service that only forwards events pays for the parse unless it sets that
  option: on the HTTP full-event path the receive alone costs 15% to 54% more
  instructions, by codec (v0.6.0 `CHANGELOG.md`, Known limitations).
- Moving a binary-mode body (`from_message_moving`) saves allocations and bytes
  and, below a few kilobytes, no instructions; a JSON body is never moved.
- Go reads Kafka record headers named `CE_id` or `Content-Type`; Java and this
  SDK refuse them (`D-KAFKA-2`, `D-INTEROP-5`).
- The module interface exports `ce::v4` only: not `ce::v3`, and not `from_v3`
  or `to_v3`.
