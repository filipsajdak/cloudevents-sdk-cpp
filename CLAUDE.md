# cloudevents-cpp — agent instructions

C++ SDK for the CloudEvents v1.0.2 specification. The full work specification is in
`docs/SPEC.md`. Read it before any task. This file holds the standing rules.

## Hard constraints

- Language floor is **C++20**. Every commit must build and pass tests under
  `-std=c++20`, `-std=c++23`, and the C++26 reflection job.
- Gate language/library features on **feature-test macros only**, never on
  `__cplusplus`. All gating lives in `include/cloudevents/detail/config.hpp`.
- `ce::core` has **no third-party dependencies except CTRE**. nlohmann/json may only
  be included from the `ce::codec_nlohmann` target.
- Any regular expression uses **CTRE** (compile-time-regular-expressions).
  No `std::regex`, no hand-rolled regex engines.
- Tests use **boost-ext/ut** via the header (`#include <boost/ut.hpp>`), not the module.
- No exceptions thrown from library code. Fallible functions return `ce::result<T>`.
- The library performs **no network I/O**. Bindings map events to/from a
  transport-neutral `ce::message`.
- The only public macro allowed is `CE_DESCRIBE`.
- Public API lives in `namespace ce::inline v1`.

## Workflow

1. Work one milestone at a time, in the order given in `docs/SPEC.md` §7.
   Do not start a milestone until the previous one meets its acceptance criteria.
2. Test-first: write the ut suite for a work item before its implementation.
3. Before declaring a milestone done, run the full matrix locally:
   `cmake --preset <preset> && cmake --build --preset <preset> && ctest --preset <preset>`
   for each of `gcc-cxx20`, `gcc-cxx23`, `clang-cxx20`, and `reflect-cxx26`.
4. Items listed in `docs/SPEC.md` §9 (Open decisions) are **not yours to decide**.
   Use the stated default, record it in `docs/DECISIONS.md`, and continue.
   If no default is stated, stop and ask.
5. If the CloudEvents spec text and `docs/SPEC.md` disagree, the CloudEvents spec wins.
   Report the discrepancy; do not silently pick.
6. Never weaken or delete a test to make a build pass. Never add `#ifdef` branches
   outside `detail/config.hpp` and the `describe` backends.
7. Small, reviewable commits: one work item per commit, conventional-commit messages.
8. Spec-first: a new behaviour needs an approved requirement in `spec/requirements/`
   before its implementation, and every test cites its requirement with a
   `// spec: SWR-AREA-NNNN` marker.

## Conventions

- Warnings are errors: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`
  (`/W4 /WX /permissive- /Zc:preprocessor` on MSVC). Deprecation warnings are errors too.
- `snake_case` for everything; `errc` enumerators `snake_case`.
- Headers are self-contained and include-what-you-use. One `#pragma once` per header.
- `constexpr` wherever the standard library at the C++20 floor permits.
- No `using namespace` at namespace scope in headers.
- Aggregates are built with **one designated initializer**, never default-constructed
  and assigned field by field. Required members first with no default member
  initializer, optional members after them with `{}`.
- Formatting by the repository `.clang-format`; run it before committing.

## Toolchain facts (measured on this machine, not remembered)

These discharge `docs/SPEC.md` §3 rule 2. Re-verify before trusting them on other hosts.

- **GCC 16.2.0 implements C++26 static reflection (P2996)**, but only behind
  `-freflection`. It accepts both `-std=c++26` and `-std=c++2c` for compilation;
  only a preprocessor-only (`-E -dM`) invocation rejects the former.
- `__cpp_impl_reflection` is `202603L` and is defined **only when `-freflection` is
  passed**. `__cpp_lib_reflection` is `202603L`, from `<meta>`. `<experimental/meta>`
  does not exist.
- `__has_include(<meta>)` is true **even without** `-freflection`, so it must never be
  the sole reflection guard; test `__cpp_impl_reflection` first.
- `std::meta::info` is consteval-only here. All reflection happens in constant-evaluated
  context; only `string_view`s, member pointers and indices may escape to runtime.
- `std::meta::access_context::current()`, `nonstatic_data_members_of` and
  `identifier_of` are available and verified working. Annotations parse.
- **GCC 16 at `-std=c++20` does not define `__cpp_lib_expected`.** That build is the
  load-bearing enforcement job for the `result<T>` polyfill subset: do not remove it.
- Homebrew Clang 23 has no reflection at all (no `<meta>`, rejects `-freflection`).

## Reference material

- Normative sources are listed in `docs/SPEC.md` §2. Fetch and read the relevant
  spec section before implementing each format or binding.
- `spec/requirements/` holds the traceable requirements derived from `docs/SPEC.md`;
  `spec/adr/` holds the architecture decisions; `docs/DECISIONS.md` holds the smaller
  judgement calls.
