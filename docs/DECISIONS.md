# Decisions

Judgement calls made while implementing `docs/SPEC.md`. Architectural decisions with
consequences live in `spec/adr/`; this file records the smaller calls and the
measurements behind them.

`docs/SPEC.md` §9 decisions D1-D7 are settled by the specification and are recorded
here only where implementation revealed something the specification did not state.

## Toolchain measurements (SPEC §3 rule 2)

SPEC §3 rule 2 requires the reflection feature-test macro names and values to be
verified against the installed toolchain rather than recalled. Measured on
Homebrew GCC 16.2.0, aarch64 macOS, 2026-09-19:

| Probe | `-std=c++20` | `-std=c++2c` | `-std=c++2c -freflection` |
|---|---|---|---|
| `__cpp_lib_expected` | undefined | 202211 | 202211 |
| `__has_include(<meta>)` | 1 | **1** | 1 |
| `__cpp_expansion_statements` | undefined | **202506** | 202506 |
| `__cpp_impl_reflection` | undefined | undefined | **202603** |
| `__cpp_lib_reflection` | undefined | undefined | **202603** |

**The reflection guard is `__cpp_impl_reflection` AND `__cpp_lib_reflection`.**
Two tempting guards are wrong, and both fail silently rather than loudly:

- `__has_include(<meta>)` is **true at plain `-std=c++2c`**. The header exists and
  self-guards on `__glibcxx_reflection`, so without `-freflection` it includes as an
  *empty* header. The result is not "header not found" but a wall of
  `'nonstatic_data_members_of' is not a member of 'std::meta'`.
- `__cpp_expansion_statements` is also true at plain `-std=c++2c`.

GCC 16 accepts **both** `-std=c++26` and `-std=c++2c` when compiling; only a
preprocessor-only (`-E -dM`) invocation rejects the former, which is what produced
the incorrect claim corrected in D-CONFIG-2 below. `-freflection` is in turn
rejected at `-std=c++20` and `-std=c++23` (`'-freflection' only supported with
'-std=c++26' or '-std=gnu++26'`). `<experimental/meta>` does not exist on GCC;
only the standard `<meta>`.

Homebrew Clang 23 and Apple Clang 21 have no reflection at all: both reject
`-freflection` and `-fexperimental-reflection` and ship no `<meta>`.

**GCC 16 at `-std=c++20` does not define `__cpp_lib_expected`.** That build is the
enforcement job for the `result<T>` subset (see ADR-0002) and must not be removed
from the matrix.

## D-CONFIG-1: `CE_HAS_*` remain live macros, contrary to a literal reading of SPEC

SPEC §10 says "no `#if` outside `detail/config.hpp` and describe backends", and
`CLAUDE.md` says the only public macro is `CE_DESCRIBE`. Taken together and read
literally these are not satisfiable, because `CE_HAS_EXPECTED` gates an `#include
<expected>` and `CE_HAS_REFLECTION` gates both an `#include <meta>` and the parsing
of `^^` splice syntax, and no `constexpr bool` can gate either.

Decision: `CE_HAS_*` stay `#define`d and are documented as internal. Library code
never reads them; it reads the `ce::detail::has_*` `inline constexpr bool` mirrors,
which keeps the source snake_case per the naming convention. `#if` on `CE_HAS_*`
appears only in `detail/config.hpp` and the describe backends, which is exactly the
allowance SPEC §10 already grants.

`CE_FIELD` is a second public macro, since SPEC §5.2 specifies it by name. The rule
is restated as: the public macros are `CE_DESCRIBE` and `CE_FIELD`; everything else
is `CE_DETAIL_*` and is `#undef`-ed by the header that defines it.

## D-CONFIG-2: the dialect comes from `CMAKE_CXX_STANDARD`, the flag does not

An earlier version of this file claimed GCC 16 rejects `-std=c++26` and must be
given `-std=c++2c`. **That is wrong**, and it is recorded here because the mistake
is easy to repeat. The claim came from a `-E -dM` preprocessor-only probe, which
does reject the spelling; ordinary compilation accepts it. Verified three ways:
`g++-16 -std=c++26 -fsyntax-only` succeeds, so does the same with `-freflection`,
and CMake 4.4 lists `cxx_std_26` for this compiler and emits `-std=c++26` for it.

The reflection preset therefore sets `CMAKE_CXX_STANDARD 26` and adds only
`-freflection` to the flags. Setting the dialect by hand does not work anyway:
`ce::core` declares `target_compile_features(... cxx_std_20)` as its floor, and
CMake appends that target's `-std=c++20` *after* anything in `CMAKE_CXX_FLAGS`, so
the last `-std` wins and reflection is then rejected for the wrong dialect. The
observable symptom is a command line carrying both `-std=c++2c` and `-std=c++20`.

The floor declaration stays as it is: `cxx_std_20` is a minimum, and a preset
raising `CMAKE_CXX_STANDARD` above it is the supported way to compile newer.

## D-CONFIG-3: MSVC requires `/Zc:preprocessor`

`/permissive-` does **not** imply `/Zc:preprocessor`, and the traditional MSVC
preprocessor miscounts `CE_DETAIL_NARG`, which would make `CE_DESCRIBE` silently
generate the wrong field list. The MSVC preset adds `/Zc:preprocessor`, and
`describe_macro.hpp` carries an `#error` on `_MSVC_TRADITIONAL` so the failure is a
diagnostic rather than a wrong answer.

## D-DESC-1: three reflection traps that fail silently

Found by compiling, not by reading the paper. Each would have produced a wrong
answer rather than an error:

1. **`std::string_view` is not a structural type in libstdc++** (`_M_len` is
   private), so `[[=ce::name("x")]]` cannot store one. The annotation type is
   `name<N>` holding `char data_[N]`, with a deduction guide.
2. **`std::meta::type_of(annotation)` yields `const skip_t`, not `skip_t`.** A
   natural `type_of(a) == ^^skip_t` comparison is therefore **always false**, and a
   field marked `[[=ce::skip]]` would be silently serialized. Use
   `annotations_of_with_type(m, ^^skip_t)`, or strip the const with
   `remove_const`.
3. **`extract<name<N>>(a).view()` dangles**, because it views a prvalue's array.
   `define_static_string` is mandatory for any name that escapes to runtime, not a
   stylistic preference.

Splices require constant expressions, which forces the
`template <std::meta::info M>` plus `template for (constexpr auto ...)` shape. That
makes expansion statements a hard prerequisite of the reflection backend, enforced
by an `#error` in `detail/config.hpp`.

## D-DESC-2: the parity suite is a no-op unless the reflection TU drops `CE_DESCRIBE`

Because the macro backend takes precedence by design (ADR-0003), a parity test whose
fixtures carry `CE_DESCRIBE` would exercise the macro path **twice** and report
success without ever running the reflection backend. The shared suite is therefore
compiled twice from one `.inl`, and the reflection translation unit compiles the
fixtures with their `CE_DESCRIBE` specializations excluded.

## D-DESC-3: the `CE_DESCRIBE` paren-probe needs double indirection

The textbook `IS_PAREN(x)` written as `SECOND(PROBE x, 0, ~)` **always returns 0**:
`SECOND`'s arguments are parsed from the replacement-list text before `PROBE` is
rescanned, so the second argument binds to the literal `0`. The probe expands in an
argument position first. `CE_DETAIL_ENTRY_1` needs the same treatment, because
`CE_DETAIL_UNPAREN e` is one argument at the call site and becomes two only after
rescan.

## D-JSON-1: `json_text` costs one parse per encode

Emitting the `json_text` alternative under `data` means parsing it through the codec
and splicing the resulting DOM, so the output is one well-formed document rather
than an escaped blob. That costs a parse on the encode path. The alternative, an
erased codec DOM stored in `core`, would put a codec dependency in `core` and break
the downward dependency rule, so it is rejected. Callers who already own a DOM get a
`to_value(event, Codec::value&&)` overload as the zero-copy path.

This is also the one place the library validates `json_text`, reporting
`errc::malformed_json` with a JSON pointer of `/data` -- which is where a caller's
mistake should surface.

## D-JSON-2: `as_string` returns a `string_view` into the codec value

This ties the view's validity to the lifetime of the DOM value it came from. Both
shipped codecs satisfy it. It would exclude a simdjson on-demand adapter, which does
not own its strings; the escape hatch, should that ever be wanted, is an
`as_string(value, std::string& scratch)` overload that costs every other codec a
copy. Not paid today.

## D-SPEC-1: the spec tree is nested, so the tool roots differ

The spec kit's `reqlib.py` computes one `REPO_ROOT` from its own location. With the
spec tree nested under `spec/`, requirement and schema paths resolve from `spec/`
while `code:`/`doc:`/`test:` references in frontmatter are repository paths. The two
roots are therefore separated into `SPEC_ROOT` and `REPO_ROOT`; the rest of the gate
logic is unmodified from the kit.

## D-SPEC-2: security classification profile

The active profile is `scudoai-eu-regulated`, unchanged from the kit. For a
protocol library the useful values are `operational` and `security-relevant`; the
latter marks every routine reachable by input from an untrusted peer (timestamp
parsing, base64, percent-decoding, UTF-8 validation, JSON and message decoding).
No new profile was added, so this repo grades the same way as the others.

## D-BUILD-1: boost-ext/ut is patched for GCC on macOS

ut 2.3.1 captures `argc`/`argv` from a function marked
`__attribute__((constructor(101)))`, guarded on compiler identity alone:

```cpp
#if (defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)) && \
    !defined(__EMSCRIPTEN__)
__attribute__((constructor(101))) inline void cmd_line_args(...)
```

Mach-O supports plain constructors but not constructor *priorities*, so GCC on
macOS rejects it outright with "constructor priorities are not supported". Clang
on macOS accepts it, and every compiler on ELF accepts it, so CI would never have
caught this -- it is exactly the class of local-only breakage that makes a green
macOS build weak evidence.

`CeDependencies.cmake` drops the priority through a `PATCH_COMMAND`, applied only
when the host is Apple and the compiler is GNU. The behaviour is unchanged: 101 is
the lowest user priority, and nothing depends on ordering against other static
initialisers because `largc`/`largv` are read later, at run time.

This is an upstream portability bug rather than a local misconfiguration; the guard
should exclude the Darwin-plus-GCC combination. Worth reporting upstream.

## D-BUILD-2: exported target names need `EXPORT_NAME`

`install(EXPORT ... NAMESPACE ce::)` prepends the namespace to the **raw** target
name, not to the alias. Targets named `ce_core`, `ce_format_json` and so on were
therefore installed as `ce::ce_core`, while every consumer writes `ce::core`.

Nothing in the source tree notices, because the in-tree `add_library(ce::core ALIAS
ce_core)` makes the intended spelling work locally. The whole test suite was green
with a package no downstream project could consume.

`set_target_properties(... PROPERTIES EXPORT_NAME core)` fixes it. The general point
is why SPEC section 7 M0 makes `test/consumer/` an acceptance criterion rather than a
nicety: an install can only be verified by installing it and building something else
against it, from outside the build tree.

## D-CORE-1: SPEC 5.1's timestamp representation cannot meet SPEC 5.1's round-trip guarantee

SPEC 5.1 specifies two things about `timestamp` that cannot both hold:

- the representation is "UTC instant (`sys_time<nanoseconds>`) plus original offset
  (`minutes`)";
- `to_string` "round-trips byte-for-byte for canonical input".

An instant plus an offset does not determine the spelling. Three canonical RFC 3339
texts collapse onto the same pair:

| text | instant | offset |
|---|---|---|
| `2018-04-05T17:31:00Z` | same | 0 |
| `2018-04-05T17:31:00+00:00` | same | 0 |
| `2018-04-05T17:31:00.000Z` | same | 0 |

`Z` and `+00:00` are both canonical and both mean a zero offset; trailing fractional
zeros are canonical and carry no information the instant records. Rendering from
instant-plus-offset alone must therefore pick one spelling and lose the other two,
so an event that arrives and leaves again would not equal itself.

**Decision:** keep `sys_time<nanoseconds>` and the offset exactly as SPEC requires,
and add two members that record only the spelling: `form` (whether the zero offset
was written `Z` or numerically) and `fractional_digits` (0-9). The instant remains
the single source of truth for *when*; these two carry *how it was written*. This is
the smaller deviation, because dropping the round-trip guarantee instead would break
SWR-CORE-0009 and the JSON round-trip property in SWR-SEC-0006.

## D-CORE-2: timestamps outside roughly 1678-2262 are rejected, not wrapped

`sys_time<nanoseconds>` counts nanoseconds in an `int64`, which spans about 584
years: roughly 1678 to 2262. RFC 3339 admits any four-digit year, so CloudEvents
timestamps exist that this representation cannot hold.

Measured, before the fix: `9999-12-31T23:59:59.999999999-12:59` parsed successfully
and rendered as `1816-03-30T05:56:08.066277375-12:59`. Silent wraparound, on a
decode path reachable by any peer that can send a request.

**Decision:** compute the instant in seconds, range-check it, and return
`errc::out_of_range` when it will not fit. Parsing runs in seconds precisely so the
check happens before the widening rather than after it, because afterwards there is
nothing left to detect.

This is a real limitation of the representation SPEC 5.1 mandates, not of the
parser. The alternative is a wider instant type, which SPEC does not ask for; a
loud, typed refusal for years outside the supported range is the honest reading, and
the range comfortably covers every timestamp a CloudEvent is plausibly carrying.

## D-CORE-3: CTRE rejects an unescaped dash in a character class

`[+-]` and `[-+]` both fail to compile; the dash must be escaped, `[+\-]`. CTRE's
diagnostic is `problem_at_position<N>` with a character offset into the pattern and
no description, so the offset has to be counted by hand against the pattern text.
Recorded because the SDK is required to use CTRE for every pattern, and this will
come up again.

## D-CORE-4: `uri` and `uri_ref` must be distinct types, not aliases

SPEC 5.1 says `attribute_value = variant<bool, int32_t, string, binary, uri,
uri_ref, timestamp>` and separately introduces `uri` and `uri_ref` alongside
`binary = std::vector<std::byte>`, which reads as though all three were aliases.

They cannot be. `std::variant` requires distinct alternatives: with `uri` and
`uri_ref` both aliasing `std::string`, the variant holds `std::string` three times,
`std::get<std::string>` is ill-formed ("T must occur exactly once in
alternatives"), and constructing the variant from a `std::string` is ambiguous.
The type does not compile at all.

The distinction is also real rather than bookkeeping. String, URI and
URI-Reference are three different types in the CloudEvents type system with an
identical wire form, so the declared type is the only thing that tells them apart,
and the typed-extension layer (SPEC 5.5) has to recover it.

**Decision:** `uri` and `uri_ref` are distinct instantiations of a small
`tagged_string<Tag>` wrapper. Construction from `std::string` and from a string
literal stays implicit, since a URI is textual and there is nothing to hide; the
variant still resolves a `std::string` to the `std::string` alternative, because an
exact match beats a user-defined conversion. A test asserts that, so the resolution
is pinned rather than assumed.

## D-CORE-5: `to_string` uses `std::format` where the toolchain has it

Rendering a timestamp by hand was fifteen `push_back` calls computing digits with
`/` and `%`. `std::format` expresses the same thing as one call with a format
string that reads like the output.

It cannot be used unconditionally. `std::format` is absent at the SPEC section 8
floor: GCC 12 ships no `<format>` at all, and `__cpp_lib_format` is undefined
there. The hand-rolled renderer therefore has to stay.

`CE_HAS_FORMAT` in `detail/config.hpp` selects between them. This puts a second
`#if` outside that header, which SPEC section 10 permits only for the describe
backends, so the allowance list is extended with the reason rather than quietly
grown: the choice is between **tokens**, not values. `std::format` cannot be named
on a branch where `<format>` was never included, so no `constexpr bool` and no
`if constexpr` can express it. That is the same reason `result.hpp` already
carries a gate.

`CE_FORCE_NO_FORMAT` compiles the fallback on a compiler that has `std::format`,
mirroring `CE_FORCE_RESULT_POLYFILL`. A `no-format` preset and a CI job use it. A
fallback that nothing compiles is untested code that will be broken by the time
the floor compiler is the one that needs it, and the floor is exactly where it
runs.

Both paths are verified against the same suite, including the byte-for-byte
round-trip cases, so the two renderers are held to identical output rather than
merely to "looks right".
