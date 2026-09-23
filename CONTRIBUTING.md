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
- **Public API lives in `namespace ce::inline v1`**, and `CE_DESCRIBE` is the only public macro.
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
