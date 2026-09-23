# Release checklist

Everything here is a gate that a machine can run. A step that reads "check that
..." without a command is not a step.

The API was fixed at `v0.1.0`: `SWR-BUILD-0006` requires a new version
namespace for any breaking change after it. A later tag may add to `ce::v1` and
may not remove from it, so anything that should go has to go before the *first*
tag of a namespace, not this one.

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
      document this one produced (`SWR-SEC-0005`).
- [ ] `git diff --exit-code test/fixtures/interop` is empty after that run, or
      the change is understood and described in the release notes.
- [ ] `cmake -S . -B build -DCE_BUILD_MODULE=ON` builds and `module_consumer`
      exits 0 on a toolchain that supports modules (`SWR-BUILD-0010`).
- [ ] The installed package is consumed from `test/consumer/` with the system
      copy of any dependency hidden, so a stale include path cannot mask a
      missing one.

## The API is frozen at the tag

- [ ] No entity in `ce::v1` is one you would rather remove. Check the `errc`
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
