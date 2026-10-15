# Contributing

Thank you for helping.
This page covers building, the spec-first workflow, and the checks a change must pass.

## Build and test

```bash
cmake --preset gcc-cxx20 && cmake --build --preset gcc-cxx20 && ctest --preset gcc-cxx20
```

Before a change is ready, run the same line for each of `gcc-cxx20`, `gcc-cxx23`, `clang-cxx20` and `reflect-cxx26`.
CI also runs `clang-cxx23`, `polyfill-cxx23`, `no-exceptions`, `no-default-codec`, `asan`, `coverage`, MSVC and the toolchain floor (GCC 13, Clang 16, libc++ 17).

The examples, and the test that compiles every C++ block in [docs/GUIDE.md](docs/GUIDE.md), need `-DCE_BUILD_EXAMPLES=ON` and Python 3:

```bash
cmake --preset gcc-cxx20 -B build/examples -DCE_BUILD_EXAMPLES=ON
cmake --build build/examples && ctest --test-dir build/examples -L example
```

A snippet in the guide is fenced `cpp` for declarations, `cpp body` for statements that are run, or `cpp nocompile` for a line that must not build.

`cmake --build <dir> --target tidy` runs clang-tidy over the headers, and `.clang-format` sets the formatting.
`./interop/run.sh` checks the SDK against the Go and Java SDKs, and needs Docker.

The `perf` workflow measures every pull request against main and against `bench/budgets.json`, and fails when an operation gets costlier.
[docs/PERFORMANCE.md](docs/PERFORMANCE.md) explains its table, how to reproduce it with Docker, and how to raise a budget for a deliberate cost.

## Mutation testing

A pull request that changes source runs [Mull](https://mull.readthedocs.io) on the lines it changes and ends its Verification with one line.
Mull is a clang plugin and must match the LLVM major version it was built for, so the script does not use the `clang-cxx20` preset's compiler.
On macOS with Apple silicon:

```bash
brew install llvm@21                      # keg-only: it does not replace your clang
mkdir -p ~/.local/opt/mull-21 && cd ~/.local/opt/mull-21
gh release download 0.34.1 -R mull-project/mull -p 'Mull-21-*macOS*.zip' && unzip -q Mull-21-*.zip
xattr -dr com.apple.quarantine .
```

Then, from the repository root:

```bash
scripts/mutation.sh origin/main                                  # every suite
scripts/mutation.sh origin/main --suites timestamp_test,core_test # the suites that cover your change
scripts/mutation.sh origin/main --dry-run                        # print the plan, build nothing
scripts/mutation.sh origin/main --no-build --suites timestamp_test # reuse the suites already built
```

It runs from any directory.

The script builds in `build/mutation` and mutates only the lines that differ from the merge base with `<base-ref>`, uncommitted edits included, in `include/cloudevents/` (see `mull.yml`).
The SDK is header-only, so every suite compiles the headers: a mutant counts as killed when any suite kills it, and as surviving when every suite that ran it missed.
`<base-ref>` is read as `git diff` reads it: the lines that differ from the merge base of `<base-ref>` and `HEAD`.
A header the base does not have counts whole, every line of it as changed, because Mull ignores a file missing from its reference; the script gives Mull a reference commit with an empty copy of each such header (nothing in your tree or refs changes).
It prints how many changed lines Mull will consider, so a base that is too narrow or too wide shows before the run.
Set `CE_MULL_LLVM`, `CE_MULL_DIR` and `CE_MULL_VERSION` for another install (the script's header lists them).

It prints each surviving mutant and the line to paste:

```text
Mutation: Mull, changed lines: 12 killed, 10 survived (0 new tests, 0 equivalent, 0 arid)
```

Triage every survivor: write a test that kills it, or mark it equivalent (no observable behaviour changes) or arid (nothing worth asserting), then pass the counts with `scripts/mutation_summary.py --new-tests N --equivalent E --arid A <build/mutation/*.mull.log>`.
The script fails loudly rather than print a clean line when it cannot tell:

| exit | meaning |
|---|---|
| 0 | a result, or no changed C++ line under `include/` ("nothing to mutate") |
| 3 | lines changed under `include/` but Mull ran no mutant on them |
| 4 | Mull did not read `build/mutation/mull.yml` or could not resolve the reference |

On 3, check that a named suite reaches the lines and that the build is current.
If the changed lines have no operator Mull mutates (renames, namespaces), or Mull cannot run, write `Mutation: none, because <reason>`.
Mutation is advice, never a merge gate.

### The pull request job

The `mutation` workflow (`.github/workflows/mutation.yml`, SWR-BUILD-0014) runs the same script on every pull request that touches `include/`, `mull.yml` or the mutation tooling, against the merge base with the target branch.
It publishes the `Mutation:` line and the surviving mutants in the job summary and in one comment that later pushes update in place.
Run it by hand from the Actions tab with a `base_ref` to see numbers for any branch.

- **It is advisory.** A surviving mutant never fails it, so it stays out of the required checks; do not add it to branch protection. Triage the survivors as above and write the triaged line in the pull request's Verification.
- **It fails only when the tool did not run** (Mull could not read its configuration, the build broke, a download failed), because a green job has to mean that mutants ran. Changed `include/` lines that no operator reaches end in a notice, not a failure.
- **Pinned:** Mull 0.34.1 for LLVM 18, installed from the `.deb` built for Ubuntu 24.04, checked against its published SHA-256 and cached by that checksum; the compiler is that release's own `clang-18` (18.1.3).
  To move it, change the four `MULL_*` values in the workflow together.
- The run uses the whole job budget (`timeout-minutes` in the workflow) because it builds every suite with the plugin; cancel a superseded run rather than waiting.

## Spec first

Every behaviour of the SDK traces to a requirement in [spec/requirements/](spec/requirements/).

1. **A new behaviour starts as a requirement.** Write it from the templates in `spec/templates/` and get it approved before the implementation.
   Changing an approved requirement takes a change request in `spec/requirements/change-request/`, and a design decision takes an ADR in `spec/adr/`.
2. **Tests come first.** Write the suite, then the code.
3. **Every suite cites its requirement** with a marker on the line above it:

   ```cpp nocompile
   // spec: SWR-CORE-0014
   "event-model"_test = [] { /* ... */ };
   ```

   The requirement names the suite back in its `verified_by`, and the gate checks both directions.
4. **A pull request includes the tests for the code it adds.**
   An implementation merged without its suites leaves its requirements reading as unstarted work.

Where the CloudEvents specification and [docs/SPEC.md](docs/SPEC.md) disagree, the CloudEvents specification wins; report the difference rather than picking one.

### The three gates

CI runs these on every pull request, and a failure blocks the build:

```bash
python3 spec/tools/lint_requirements.py
python3 spec/tools/check_trace.py
python3 spec/tools/check_references.py
```

They need `pyyaml` and `jsonschema`.
[lefthook](https://github.com/evilmartians/lefthook) runs them before every push, from a virtual environment at `.venv`:

```bash
python3 -m venv .venv
./.venv/bin/pip install -r spec/tools/requirements-dev.txt
lefthook install
```

## Rules for the code

- **C++20 is the floor.** Every commit builds and passes at C++20, C++23 and the C++26 reflection job.
- **Features are gated on feature-test macros**, never on `__cplusplus`, and only in `include/cloudevents/detail/config.hpp`.
- **No exceptions.** A fallible function returns `ce::result<T>`.
- **No dependencies in `ce::core`** except CTRE. Every regular expression is CTRE; nlohmann/json appears only behind `ce::codec_nlohmann`.
- **No network I/O.** A binding maps an event to and from a `ce::message`.
- **Public API lives in `namespace ce::inline v4`**, and `CE_DESCRIBE` is the only public macro.
  `ce::v3` holds what v0.5.0 published, under `include/cloudevents/v3/`, `ce::v2` holds what v0.4.0 published, under `include/cloudevents/v2/`, and `ce::v1` holds what v0.3.0 published, under `include/cloudevents/v1/`; a declaration or documented behaviour in any of them never changes.
- **Build a value in one braced expression**: a designated initializer for an aggregate, an initializer list for a container. Required members come first with no default member initializer.
- **Every time quantity is a `std::chrono` type**, intermediates included.
- **Warnings are errors**: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`, or `/W4 /WX /permissive-` on MSVC.
- **Header comments are `// spec: SWR-AREA-NNNN` and `// TODO(#NN):` markers only.**
  The caller-facing contract goes in the guide, the rationale in a requirement, ADR or [docs/DECISIONS.md](docs/DECISIONS.md), and a trap in a named test.
- **Never weaken or delete a test to make a build pass.**

Tests use [boost-ext/ut](https://github.com/boost-ext/ut) through `#include <boost/ut.hpp>`.
A rule that holds at compile time is tested by a must-not-compile probe; `test/attribute_literal_probe.cpp` shows the shape.

## Commits and pull requests

- One work item per commit, with a [conventional commit](https://www.conventionalcommits.org/) subject such as `fix(binding): ...`.
- Each commit builds and passes on its own.
- Commit messages and pull request text are ASCII only.
- Run `clang-format` on what you changed.
