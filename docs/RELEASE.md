# Release checklist

Everything here is a gate that a machine can run. A step that reads "check that
..." without a command is not a step.

`v0.1.0` is the first tag, so the API is fixed at that moment: `SWR-BUILD-0006`
requires a new version namespace for any breaking change afterwards. Anything
that should be removed from `ce::v1` has to go before the tag.

## Before tagging

- [ ] `main` is green: all 13 CI jobs, not only the compiler matrix.
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
- [ ] Each fuzz target has run ten minutes clean on the release commit
      (`SWR-SEC-0001`). The nightly job counts only if it ran on that commit.
- [ ] `./interop/run.sh` regenerates the goldens and both SDKs accept every
      document this one produced (`SWR-SEC-0005`).
- [ ] `git diff --exit-code test/fixtures/interop` is empty after that run, or
      the change is understood and described in the release notes.
- [ ] `cmake -S . -B build -DCE_BUILD_MODULE=ON` builds and `module_consumer`
      exits 0 on a toolchain that supports modules (`SWR-BUILD-0010`).
- [ ] Doxygen produces no warnings: the docs job treats them as errors, because
      SPEC section 10 requires a brief on every public symbol.
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
- [ ] Tag `v0.1.0` on `main`, signed.
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
