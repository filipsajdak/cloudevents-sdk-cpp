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

## D-CORE-5: `std::format` is required, and the floor rises to meet it

`to_string` uses `std::format` and keeps no second implementation. Rendering by
hand was fifteen `push_back` calls computing digits with `/` and `%`; the format
string now reads like the output it produces.

This raises the toolchain floor, which SPEC section 8 said may be raised "only if
CTRE or ut require it". Neither does. The floor moved because the specification's
owner decided a second renderer was not worth carrying, and SPEC section 8 is
updated to match rather than left contradicting the code.

**The constraint is the standard library, not the compiler.** Measured:

| toolchain | `<format>` | `__cpp_lib_format` | builds the SDK |
|---|---|---|---|
| GCC 12 (libstdc++ 12) | absent | undefined | no |
| GCC 13, 14, 16 | present | defined | yes |
| Clang 16 + libstdc++ 12 | absent | undefined | no |
| Clang 16 + libc++ 16 | **present** | **undefined** | **no** |
| Clang 16 + libstdc++ 14 | present | defined | yes |
| AppleClang 21 (libc++) | present | defined | yes |

The libc++ 16 row is why the guard tests `__cpp_lib_format` rather than
`__has_include(<format>)`. That library ships the header while the feature is
incomplete, so it advertises nothing, and `__has_include` would wave it through to
fail later and less clearly. This is the same trap as `__has_include(<meta>)` in the
reflection guard, and the same answer.

So a compiler version alone never decides it: the same Clang 16 builds the SDK or
does not, depending on which library it is paired with. `detail/config.hpp`
therefore fails with a named `#error` naming the three libraries that qualify,
rather than letting the build collapse into "`<format>` file not found" followed by
a hundred errors about `std::format`.

The alternative was the one just deleted: keep both renderers, gate them on
`CE_HAS_FORMAT`, and compile the fallback on a dedicated preset and CI job so it
could not rot. That works, and it cost a preprocessor conditional in a header that
now has none, a preset, a CI job, and a second body of code held to the same tests
as the first. Dropping GCC 12 buys all of that back.


## D-DESC-4: CE_DESCRIBE defines an ADL hook, not a specialization

An explicit specialization of a `ce::` template must appear at global scope or in
a namespace enclosing `ce`. A user type in `my::app` could therefore not be
described beside itself, which is where SPEC 5.2 says the macro goes.

`CE_DESCRIBE` instead defines `ce_describe_fields(describe_tag<T>)`, found by ADL
because `describe_tag<T>` associates T's own namespace. The macro then works in
the namespace that declares the type.

One consequence worth knowing: a type in an unnamed namespace associates that
namespace, not the enclosing one, so the macro has to sit inside it too. The
parity suite does exactly that.

## D-DESC-5: the parity suite is checked for being a no-op, not assumed not to be

Macro precedence means a parity fixture carrying `CE_DESCRIBE` exercises the macro
path twice and reports success while the reflection backend goes uncompiled.

Two things rule that out. The reflection fixtures carry no `CE_DESCRIBE` at all,
and the reflection-only test names appear in the `reflect-cxx26` binary and in no
other, which is checkable from outside the suite.

Then the suite was broken on purpose: the annotation lookup was pointed at the
wrong annotation type, so a renamed member would report its identifier instead.
The build fails, at compile time, on the `static_assert` comparing the two
backends' names. Compile-time is the stronger outcome, because it cannot be
skipped by a stale binary.

That last point is worth stating plainly, because the first attempt at this check
was wrong. It ignored the build exit code and ran ctest anyway, which ran the
previous binary and reported a pass. A verification step that does not check
whether the build succeeded proves nothing at all.

## D-CI-1: Clang's C++23 job uses libc++ 17, and the format gate carves it out

Clang could not be tested at C++23 at all, and fixing that showed the
`std::format` gate refusing a library that works.

**libstdc++ cannot pair with clang 16 at C++23.** The job failed inside
`<ranges>` ("requires clause differs in template redeclaration", "type-id
cannot have a name"), with no SDK header in the stack, on libstdc++ 14 and on
the SPEC floor of 13 alike. Their C++23 `<ranges>` uses language features
clang 16 does not implement, so the pairing is untestable rather than
untested. clang-16 now proves libstdc++ 13 at C++20, pinned with
`--gcc-install-dir` because ubuntu-24.04 would otherwise supply 14.

**libc++ implements std::format well before it says so.** Measured by
printing the macro from `<version>` and compiling a real `std::format` call
in the `silkeh/clang` images:

| libc++ | `__has_include(<format>)` | `__cpp_lib_format` | `std::format` call |
|---|---|---|---|
| 16 | true | undefined | no member named 'format' |
| 17 | true | undefined | works |
| 18 | true | undefined | works |
| 19 | true | 202110 | works |

`-fexperimental-library` changes none of it. cppreference lists libc++ 17 as
supporting the feature, and that is correct; libc++ withheld the macro until
19.

Gating on the macro alone would refuse two releases that serve the SDK, so
`detail/config.hpp` carves out `_LIBCPP_VERSION >= 170000`. The SDK formats
only integers and strings (`detail/timestamp.hpp`), which is well inside what
libc++ 17 provides.

`__has_include(<format>)` is not the carve-out, because it is true on libc++
16, where the header exists and `std::format` is not in it. That would trade
a named `#error` for "no member named 'format'" - the same false positive
`__has_include(<meta>)` produces for reflection (D-CONFIG-2).

Verified by building `parse_timestamp` and `to_string` against each library:
16 stops at the named `#error`; 17 and 18 round-trip
`2026-09-20T12:34:56Z`. The C++23 job uses libc++ 17 deliberately, because a
newer one would leave the carve-out unexecuted.

## D-EXT-1: An extension field may declare any attribute type except Binary

`event::get<Ext>()` and `event::set()` accept fields of type `bool`,
`std::int32_t`, `std::string`, `uri`, `uri_ref` and `timestamp`, each
optionally wrapped in `std::optional`. That is the CloudEvents attribute type
system exactly, less Binary.

Binary is out because recovering one from its wire form needs base64, which
lives in the format layer, and core may not reach into it. None of the five
documented extensions declares a Binary attribute, so nothing is lost today. A
struct that declares one fails a `static_assert` naming the permitted types
rather than failing somewhere inside the mapping.

The describe seam's own supported set is wider in one direction (`int64_t`,
`double`, `vector`, `map`) and narrower in another (no `uri`, `uri_ref` or
`timestamp`), because it exists to map JSON payloads. The two sets are
deliberately separate: an extension attribute is not a payload.

## D-EXT-2: Two extension structs name their field `value`

`sequence` and `dataref` each define a single attribute whose name equals the
struct's. A member named `sequence` inside `struct sequence` hides the injected
class name, so both use `value` with the wire name given explicitly:

```cpp
CE_DESCRIBE(sequence, CE_FIELD(value, "sequence"));
```

The wire name is what the CloudEvents spec fixes, and it is what the tests
assert. The other three name their fields after their attributes, because no
collision arises.

## D-EXT-3: `sampledrate > 0` is checked on the struct, not on the event

The sampling extension requires a rate above zero. `event::validate()` does not
check it, because core cannot know which extensions a given event is carrying,
and an event that merely holds a `sampledrate` attribute is still a valid
CloudEvent by the core spec.

`ext::sampled_rate::validate()` is where the constraint lives, so a caller that
has asked for the typed view gets the check and one that has not is not
second-guessed.

## D-EXT-4: The typed payload accessors take the codec as a template parameter

SPEC 5.5 writes them as `event::data_as<T>(codec)` and
`event::set_data(const T&, codec)`. They ship as free function templates,
`data_as<T, Codec>(event)` and `set_data<T, Codec>(event, value)`, for two
reasons.

They cannot be members: SWR-EXT-0006 requires that core name no codec, and a
member would put one in `core.hpp`. A test pins that by asserting `ce::event`
has no such member.

The codec is a template parameter rather than a value because that is how
`json_format<Codec>` and the HTTP binding already take theirs. A codec is a
set of statics with no state to pass.

`event::get<Ext>()` and `event::set()` stay members, as SPEC 5.5 writes them,
because an extension attribute needs no codec at all.

## D-EXT-5: A described struct decodes with absent members left at their default

`from_json_value` fills only the members the document carries. An absent member
leaves the field value-initialized rather than failing, so an optional field
need not be written and a struct that grows a field still reads older
documents.

A member that IS present must match the declared type, and an integer that does
not fit its declared field is `out_of_range` rather than a silent truncation:
a wrapped value would make the decoded struct disagree with the document it
came from.

## D-SEC-1: `errc::unsupported_field_type` removed before the first tag

The enumerator existed and nothing could produce it. Its only occurrence outside
the enum was the case in `to_string_view`.

`SWR-SEC-0003` requires a negative test per `errc` that presents input causing
that error. For a code the SDK cannot emit, no such input exists, so the
requirement was unsatisfiable rather than merely unmet.

The condition it described - a described struct with a member type the SDK
cannot map - is caught by `static_assert` in `describe.hpp` and `core.hpp`,
which names the struct at the point of use. A compile-time diagnostic is
strictly better here, so the runtime code was never written and the enumerator
was vestigial.

Removing it renumbers `invalid_argument` from 16 to 15, which
`test/build_test.cpp` pins. `SWR-BUILD-0006` requires a new version namespace
for a breaking change, so before `v0.1.0` is the only moment this is free.

## D-SEC-2: The conformance fixtures are stored verbatim, placeholders and all

`SWR-SEC-0004` requires every example from the core, JSON format and HTTP
binding documents. Nineteen exist, and several are illustrations rather than
wire bytes: `Content-Length: nnnn`, `... application data ...`,
`...raw binary bytes...`, `"... base64 encoded string ..."`.

They are stored as published, with no placeholders filled in. A fixture that had
been repaired would no longer be the specification's example, which is the one
thing it is for.

`conformance_test.cpp` carries a table saying which are elided, and asserts that
classification by scanning the bytes. A revision that replaces a placeholder
with real content fails that assertion, which is the signal to give the fixture
a real decode rather than leaving it on the weaker path unnoticed.

`json-01` and `json-09` are elided only inside `data_base64`. They decode as far
as that member and are then rejected with `invalid_base64`, which is the correct
answer for the published bytes; the suite asserts that, and asserts the same
documents decode once the placeholder is replaced with real base64.

**The fixtures found a conformance bug.** `json-03` carries
`"unsetextension": null`, and the decoder rejected it. JSON format section 2.2:
*a null value encountered while decoding an attribute MUST be treated as the
equivalent of unset or omitted*. The optional context attributes already did
this; extensions did not. `SWR-JSON-0032` now states the rule. `data` is
excluded, because the specification makes an explicit null payload distinct from
an absent one.

An existing test asserted the old behaviour, listing `null` beside an object and
an array as a type mismatch. That test was wrong against the specification and
was corrected rather than deleted.

## D-INTEROP-1: The SDKs disagree about how to carry a non-JSON payload

Given the same `text/plain` payload of the same bytes:

| producer | representation |
|---|---|
| Go | JSON string under `data` |
| this SDK | JSON string under `data` |
| Java | `data_base64` |

Both are permitted. The Java API was handed a `byte[]`, and its serializer maps
bytes to the binary representation regardless of the content type.

A consumer that assumes either one breaks against the other. `interop_test.cpp`
therefore asserts that the payload **bytes** agree across producers rather than
that the representation does, and pins the divergence in a test of its own so it
is a known fact rather than a surprise during an upgrade.

This is why `ce::data_t` is a variant a caller inspects, rather than an
accessor that guesses from `datacontenttype`.

## D-MODULE-1: What the module interface can and cannot do

`cloudevents.cppm` compiles, links and is importable. `module_consumer.cpp`
builds an event, parses a timestamp, round-trips a typed extension and calls
base64 entirely through `import cloudevents;`. Two constraints were measured
getting there, and both are properties of the toolchain rather than of the SDK.

**The using-declarations must name `ce::v1`, not `ce`.** The public API lives in
`namespace ce::inline v1`. Writing `export namespace ce { using ce::event; }`
redeclares each name into the scope it already occupies and exports nothing; the
consumer then reports that `ce` has not been declared, which points nowhere near
the cause.

**An importing translation unit may not include a standard header the module's
global module fragment already absorbed.** Under GCC's `-fmodules-ts`, a
consumer that does gets `redefinition of 'constexpr bool
std::__is_constant_evaluated()'` and conflicting `pthread` declarations from
inside libstdc++, naming nothing in the consumer. `module_consumer.cpp`
therefore includes no standard header and uses member functions rather than the
free comparison operators, which come from `<string>`.

**`CE_DESCRIBE`'s hook has to be exported too.** It defines a function found by
ADL, and a function declared in the global module fragment is not reachable from
an importing translation unit unless exported. Without
`using ce::v1::ext::ce_describe_fields;`, `described<ce::ext::tracing>` is false
on the far side of the boundary and `get<>`/`set<>` do not resolve.

This unevenness is the reason `SWR-BUILD-0010` keeps the module out of the
default build, and the reason `CE_BUILD_MODULE` never switches itself on.

## D-SEC-3: Coverage is measured on Linux, and gcovr is told which gcov to use

`gcovr` defaults to `gcov`. On macOS that is Apple's, which cannot read GCC's
`.gcda`, and it does not fail: it reports every header as **0 percent**. A
coverage gate that reports zero and a coverage gate that reports nothing are
equally useless, and the first looks like a result.

`CE_GCOV_EXECUTABLE` therefore names the `gcov` matching the compiler, and the
CI job passes `gcov-13`.

Local macOS measurement stayed unreliable even so: with `gcov-16` named
explicitly, `base64.hpp` reported 0 of 65 lines from a suite that calls it
twenty-two times at run time. The coverage job runs on Linux, and that job is
the measurement `SWR-SEC-0007` refers to.

The floor is enforced by `gcovr --fail-under-line`, so it fails the build rather
than printing a number. Measured on the first run of the coverage job: **95.9
percent of lines** (1021 of 1065), 88.3 percent of functions, 60.5 percent of
branches. The line floor of 90 is met.

## D-SEC-4: The JSON parsers carry a nesting depth limit

`fuzz_json_decode` reached a stack overflow after 2.37 million executions in
CI. Deeply nested input, one small document, no memory safety violation
anywhere: recursive descent with no limit simply runs out of stack.

The shipped codec was never affected. Measured against a well-formed document
nested 100,000 deep:

| parser | before |
|---|---|
| `nlohmann_codec` | parses it; nlohmann is not recursive here |
| `mini_codec` | stack overflow |
| `examples/custom_codec` | same shape, same exposure |

Both of those are demonstrations rather than shipped code - `mini_codec` is the
second codec the format layer is tested against, and the example exists to be
copied. That is precisely why the example needed fixing: a reader who takes it
as a starting point inherits the defect into something that does ship.

Both now refuse input nested beyond 100 levels with a `parse_error`, and the
depth is restored by a guard object so an error path cannot leak it. The limit
is a constant rather than an option: a caller who needs deeper nesting in a
CloudEvent has a different problem.

## D-ADOPT-1: nlohmann is looked up softly, so the package stays usable without it

The package configuration called `find_dependency(nlohmann_json 3.12.0)` whenever
the SDK had been built with the codec. `find_dependency` is a hard failure, so
`find_package(cloudevents)` failed outright when nlohmann was absent - including
for a consumer that wanted only `ce::core`, which depends on CTRE alone
(SWR-ADOPT-0002).

The defect was invisible everywhere it was looked for. On a machine with a system
nlohmann, the lookup succeeds. On a runner without one, FetchContent stages a copy
into the same prefix as the SDK, and the lookup succeeds against that. It appears
only in the combination a distribution actually produces: built against the system
copy, staging nothing, consumed by someone who does not have nlohmann.

It surfaced while working the release checklist, on the item that says to consume
the installed package "with the system copy of any dependency hidden". That step
existed precisely to catch this, and it did, on its first honest run.

The configuration now uses `find_package(... QUIET)` and reports `codec_nlohmann`
as a component. A consumer that needs the codec asks for it by name and gets a
failure that says which dependency is missing; a consumer that does not is
unaffected. `SWR-ADOPT-0005` states the behaviour, and the install-and-consume job
now reinstalls against a system nlohmann and consumes with it hidden, which is the
only arrangement that can see the problem.

## D-INTEROP-2: Both in-tree codecs mishandled a surrogate pair

A character outside the Basic Multilingual Plane is written in JSON as a
surrogate PAIR of `\uXXXX` escapes, and the Java CloudEvents SDK writes one for
every emoji. Given `😀`:

| codec | produced | |
|---|---|---|
| `mini_codec` | `3F 3F` | two question marks, silent loss |
| `examples/custom_codec` | `ED A0 BD ED B8 80` | CESU-8, not valid UTF-8 |
| `nlohmann_codec` | `F0 9F 98 80` | correct |

`mini_codec` said so in a comment - "enough for the ASCII the conformance corpus
uses" - which was true until the interop corpus carried a Java document with an
emoji in the subject. The example said nothing, and it is the one people copy.

The CESU-8 case is the worse of the two. It is not a corrupted character that a
reader would notice; it is a byte sequence the HTTP binding's own
`is_valid_utf8` rejects, because encoded surrogates are exactly what that check
exists to refuse. The failure would surface on an unrelated request, far from
the codec that caused it.

Both now combine the pair into one code point, and refuse a lone surrogate
rather than encoding it. `codecs-decode-surrogate-pairs` covers it.

**A note on how this hid.** The first two attempts to reproduce it in a test
appeared to show both codecs working, because the escape was written in a C++
source literal and the compiler reinterpreted it before the codec ever saw it.
The regression test builds the escape at run time from a backslash and the
characters `u`, `D`, `8`, `3`, `D`, for that reason.

## D-INTEROP-3: Timestamp equality is textual, and that is visible across SDKs

`ce::timestamp` stores the instant, the offset, how the offset was written and
the number of fractional digits, and compares all four. That is what
`SWR-CORE-0009` requires: re-emitting a received event must not alter a `time`
value a peer may have signed or compared as text.

The consequence appears the moment two SDKs describe the same moment
differently. Given `2026-09-20T12:34:56.123456789+02:00`:

| producer | emits |
|---|---|
| Go | `2026-09-20T10:34:56.123456789Z` |
| Java | `2026-09-20T12:34:56.123456789+02:00` |
| this SDK | `2026-09-20T12:34:56.123456789+02:00` |

Go marshals a `time.Time`, which carries no offset. The instants are identical.
`operator==` reports the timestamps unequal, and therefore the events unequal,
where Go and Java would both say the times match.

A caller comparing events across producers has to compare `time->utc`. That
member is public and documented as "the instant, normalised to UTC", so the
capability is there, but nothing names it. Whether to add a `same_instant`
helper is an API decision and is recorded as open rather than taken here.

## D-INTEROP-4: This SDK does not escape a non-ASCII source; Go and Java do

Given a source containing `événement`, Go and Java both emit
`%C3%A9v%C3%A9nement`, because each holds the value in a URI type that escapes
on serialization. This SDK emits the characters as given, because SPEC 5.1
checks `source` for non-emptiness only and the SDK does not parse URIs.

All three accept both spellings, so this is not an interoperability failure.
It is a difference a producer should know about: a value that must be a strict
RFC 3986 URI-reference has to be escaped before it is handed over, because
nothing downstream will do it.

## D-JSON-3: A codec may refuse an unrepresentable integer at parse or at as_int

`nlohmann::json::get<std::int64_t>()` reinterprets an out-of-range unsigned value
rather than refusing it. `18446744073709551615` arrived as `-1`, which is inside
the CloudEvents `Integer` range, so the format layer's own bounds check did not
catch it and a decode produced an extension holding a number the document never
carried. `9223372036854775808` was masked only by accident: it reinterprets to
`INT64_MIN`, which the int32 check then rejects for the wrong reason.

The defect surfaced while writing a second codec. Boost.JSON's accessor
range-checks an unsigned value before narrowing, which made the absence of that
check in the shipped codec visible. Nothing in the suite had reached for a number
above `int64`, because nothing had reason to.

The concept now states three rules the in-tree codecs disagreed about:

- `kind_of` follows the **text**. A number written without a fractional part or an
  exponent is `kind::integer` whatever its magnitude. Reporting a large integer as
  `floating` would make the format layer diagnose it as a fractional extension
  value, which is a different and wrong complaint.
- `as_int` returns `out_of_range`, not `type_mismatch`. The value is an integer,
  and one that merely exceeds the 32-bit `Integer` type already reports
  `out_of_range`; two sizes of the same mistake should not report two codes.
- A codec may instead refuse the document at `parse`, which is equally conformant.
  `mini_codec` does, because its hand-written parser uses `from_chars`. What is
  forbidden is returning a value that is not the one on the wire, and that is what
  the suites assert - the error code is checked as a disjunction.

`size_of` is documented rather than changed: the SDK never calls it, nlohmann
returns 1 for a scalar where the others return 0, and narrowing that under a
frozen `v1` would buy nothing.

## D-CODEC-1: RapidJSON ships with a stateless allocator and a stated hazard

The benchmark (PR #14) measured RapidJSON fastest on every event-sized document,
smallest binary and shortest compile, so it is the second shipped codec. Three
things about it needed deciding rather than copying.

**The allocator.** RapidJSON wants one at every mutation and the concept passes
none. The codec uses a shared `rapidjson::CrtAllocator`, not the default
`MemoryPoolAllocator`: a pool does not return memory until it is destroyed, so a
pooled codec would be a process-wide arena that only grows - faster in a
benchmark, unbounded in a service. `CrtAllocator` is stateless and forwards to
`malloc`/`free`, which the C standard requires to be thread-safe, so the shared
instance is not shared state. A `static_assert` pins the choice, because `parse`
moves the root out of the document and that is sound only while the allocator
holds nothing.

**Pointer invalidation.** `find` returns a pointer into a contiguous member
array, so a later `set` on the same object may invalidate it. nlohmann's DOM is
node-stable and does not behave this way, so code that is correct against one is
wrong against the other. `json_format` never mutates a document it is reading, so
the SDK cannot observe it - but a caller using the codec directly can, and the
guide says so.

**Two defects in the prototype, both fixed here.** `kind_of` reported an integer
above `INT64_MAX` as `kind::floating`, which would make the format layer diagnose
it as a fractional extension value - a different and wrong complaint. And `as_int`
refused the same value with `type_mismatch` rather than `out_of_range`. Both now
follow the rules D-JSON-3 wrote into the concept.

`codec-headers-are-mutually-isolated` reads the headers and refuses a codec that
names another codec's library. It strips comments first: `rapidjson.hpp` explains
how nlohmann's DOM differs, and saying so is the point of the comment rather than
a dependency.

## D-CODEC-2: Boost.JSON ships, needs exceptions, and is never vendored

Boost.JSON is the third shipped codec: the benchmark measured it fastest on the
64 KiB document and it is the closest fit to the concept, since a
`boost::json::value` is self-contained and an object member is addressable.

Three things had to be decided rather than copied.

**`get_*`, never `as_*`.** Boost.JSON's `as_object()`, `as_array()` and the
scalar `as_*` accessors throw; the `get_*` forms assert. The SDK supports
`-fno-exceptions`, so the codec uses `get_*` throughout. The prototype used
`as_object()`/`as_array()` in `set`/`push` while already using `get_*` on the
read paths - and the difference is invisible in any build that has exceptions,
which is every build that had run it.

**It cannot be built without exceptions at all, and the refusal is deliberate.**
Measured: `<boost/json.hpp>` *compiles* under `-fno-exceptions`, so the answer is
not the simple one. It fails at link, on
`boost::throw_exception(std::exception const&, boost::source_location const&)` -
a function Boost requires the **program** to define once `BOOST_NO_EXCEPTIONS` is
inferred. That function decides what happens when Boost reports an error:
terminate, abort, longjmp. It is an application's policy, and defining it inside
an SDK would make that choice for every consumer silently.

So the codec refuses the combination, in the header with an `#error` and at
configure time with a message naming the option. A CI job asserts the refusal.

**Parentheses, never braces.** `boost::json::value` has an `initializer_list`
constructor for arrays. On Boost 1.83 - the version Ubuntu 24.04 ships -
`value{nullptr}` and `value{true}` select it and produce a one-element **array**;
on Boost 1.92 the same spelling resolves to the scalar constructors. So the codec
passed every local test and failed on CI, and the failure was
`kind_of(make_null()) != null`.

This is the Glaze trap from the benchmark, in a second library:
`glz::generic_json{std::string{...}}` also compiled and yielded an array. Braces
invite an `initializer_list` overload; parentheses cannot select one. Every value
constructor in this codec uses parentheses.

**No FetchContent fallback.** Boost.JSON is a compiled library, unlike every
other dependency here. An INTERFACE target cannot supply the translation unit it
needs; asking each consumer to add one, in exactly one TU per shared object, is
an ODR trap; standalone header-only mode was removed upstream in 1.81; and the
Boost superproject is gigabytes. A request that cannot be honoured fails at
configure time naming the package to install.

A related constraint worth knowing: because it is compiled, the consumer's
compiler and standard library must match the ones Boost was built with. On macOS
a Homebrew Boost is built against libc++, so a GCC/libstdc++ build compiles the
header and then fails to link on mangling differences. That is an ABI mismatch,
not a defect, and the guide says so.

## D-BUILD-3: A codec header may carry a conditional that only refuses

`SWR-BUILD-0002` keeps capability gating in `detail/config.hpp`, because gating
spread across headers makes the supported matrix unreadable. The Boost.JSON
refusal is an `#if` in a codec header, and `config-gating-single-header` caught
it immediately - the rule working as intended.

The rule is now stated more precisely rather than widened. A codec header may
carry a conditional whose block contains **nothing but `#error`**. Such a block
selects no implementation, so there is no second path to read, and config.hpp
cannot restate the requirements of every optional codec without knowing about
each one.

The test enforces exactly that distinction: a block containing code, or an
`#else`, still fails. Both were checked by introducing them and watching the
suite catch each.

## D-KAFKA-1: The Kafka binding has no batch mode

The Kafka protocol binding defines binary and structured mode and says nothing
about batches. The batch content type belongs to the HTTP binding, and a record
carrying a JSON array under `application/cloudevents-batch+json` is something no
other SDK's consumer reads, so producing one would be an interoperability defect
rather than a useful extension.

`to_message` and `to_record` refuse `content_mode::batched` with
`errc::invalid_argument`. On receive the batch content type is recognised
**before** the structured one, because `application/cloudevents-batch+json` also
starts with `application/cloudevents`: without that ordering a batch record
reaches the format layer as a malformed object and the error describes the
document rather than the mode.

## D-KAFKA-2: Record header keys compare byte for byte

The binding specification does not state a case rule for record header keys. It
does not need to: a Kafka record header key is an opaque byte string, so there is
no case-folding to apply, and every SDK writes the names in the lowercase the
examples use.

The binding therefore sets `case_sensitive_names`, which is the strict reading.
The alternative would accept `CE_ID` as the id attribute from a producer that
never sent it, and would erase a `CE_ID` header a caller had deliberately set
alongside the binding's own.

`content-type` is matched the same way, and it is the one place the strictness is
visible: a producer emitting `Content-Type` on a Kafka record is not matched. No
SDK does, and the binding examples are lowercase throughout.

## D-HTTP-1: Percent-encoding is conformant by default and opt-out

The HTTP binding specification section 3.1.3.2 requires space, double-quote,
percent and anything outside U+0021-U+007E to be percent-encoded in a header
value. Measured on 2026-09-21 by running sdk-go v2.15.2 and by reading sdk-java
main, neither SDK encodes on send or decodes on receive.

What that costs, with the values each side actually puts on the wire:

| event | C++ sends | Go application sees |
|---|---|---|
| `subject = "a b"` | `ce-subject: a%20b` | `a%20b` |
| `subject = "100%"` | `ce-subject: 100%25` | `100%25` |

| event | Go sends | C++ produces |
|---|---|---|
| `subject = "a b"` | `ce-subject: a b` | `a b` |
| `subject = "100%"` | `ce-subject: 100%` | rejected, truncated escape |
| `subject = "100%41"` | `ce-subject: 100%41` | `100A` |

The default stays conformant. The specification is what a receiver is entitled
to expect, a released version already behaves this way, and a silent change
would be worse than a documented incompatibility. `ce::http::literal_values` is
the opt-in for callers who must talk to Go or Java, named at the call site so
the departure is visible in the code that made it.

Only binary mode is affected, and only for values containing space, double
quote, percent or a byte outside printable ASCII. Structured mode carries the
event in the body, so it is unaffected, as is the Kafka binding, where neither
this SDK nor the others escape anything.

`literal_values` is not "do nothing". Percent-encoding was also what kept a
control character out of a header field: a value carrying CR or LF would end
the header and start another one of the sender's choosing. The literal policy
refuses a control character rather than passing it through.

## D-NATS-1: Binary mode follows the binding on `main`, not the v1.0.2 tag

The NATS binding this SDK first implemented said NATS "will only support
_structured_ data mode at this time", because "the NATS protocol does not
support custom message headers, necessary for _binary_ mode". That is the
v1.0.2 text and it has been obsolete since NATS 2.2 introduced headers in 2021.
The binding on `main`, version 1.0.3-wip, now says "Every compliant
implementation SHOULD support both structured and binary modes".

So binary mode here follows a work-in-progress document rather than the tag the
rest of the SDK implements. The alternative was to follow sdk-go, the only SDK
shipping a header-based NATS mapping, and that is worse: it would mean copying
behaviour with no normative text behind it.

Three rules are worth naming because they are where sdk-go's
`protocol/nats_jetstream` differs from the binding, reported as sdk-go#1334:

| rule | binding on `main` | this SDK | sdk-go |
|---|---|---|---|
| datacontenttype | `ce-datacontenttype`, prefixed like any attribute | same | `content-type`, unprefixed |
| deciding the mode | structured when a CloudEvents content type is present, binary otherwise | same | binary when `ce-specversion` is present |
| header values | percent-encoded | same | not encoded |

The third is `D-HTTP-1` in a second binding, so `ce::http::literal_values` has
no NATS counterpart yet on purpose: no other SDK ships a NATS binary mode to be
compatible with, and inventing a compatibility mode for one implementation's
divergence would make this SDK the third behaviour rather than the second.

**If the specification changes before 1.0.3 is cut, this follows it.** That is
the cost of implementing a document marked work in progress, and it is recorded
here so the change is expected rather than surprising.

## D-NATS-2: The payload entry points stay

`to_payload` and `from_payload` predate binary mode and exchange text rather
than a `message`. They are not deprecated by `to_message` and `from_message`.

They are the correct API for a server before NATS 2.2, which cannot carry
headers at all, and `ce::v1` is frozen: `SWR-BUILD-0006` permits adding to it
and not removing from it. A caller on 2.2 or later wants the message pair.

## D-TIDY-1: The tidy target lands before the tidy gate, and the baseline is 469

`.clang-tidy` has been in this repository since M0, with a curated check set,
`WarningsAsErrors: '*'`, and a named reason beside every exclusion. **Nothing ever
ran it.** No CI job, no CMake target, no hook. Measured 2026-09-22: the first run
of the existing configuration over the existing headers reports **469 findings**.

That is the cost of a lint nobody invokes. The configuration is not wrong - it is
careful, and its exclusions are well argued - but a standard that is never
checked is a statement of intent, and the tree drifted past it for the project's
whole life without anyone being able to see it happening.

So this is split. The `tidy` target lands now: it makes the number knowable and
lets anyone reproduce it. The gate does not, because the only ways to make CI
green today are to fix 469 findings in one change or to switch off the checks
that report them - and `.clang-tidy`'s own header says what happens then: "A lint
that fails on the code it is meant to guard gets switched off."

The findings sort into three kinds, and each needs a different answer:

- **Real.** `bugprone-unchecked-optional-access` (26, concentrated in
  `binding/common.hpp`) and `bugprone-signed-bitwise` (3, at `percent.hpp:97`,
  which is the `(high << 4) | low` the audit already flagged as
  implementation-defined above 0x7F). These get fixed.
- **Satisfiable by being clearer.** `pro-bounds-avoid-unchecked-container-access`
  (75) is mostly the guarded index arithmetic in the UTF-8 walker and
  `percent_decode`. The guard is real; the checker cannot see it. Restructuring
  to express the bound is better code, not a workaround.
- **Wrong for this codebase.** Whatever remains gets disabled with a reason
  beside it, in the style the file already uses.

`readability-magic-numbers` and `cppcoreguidelines-avoid-magic-numbers` stay
disabled until that triage is done. The constants they would have found are named
now, but turning the checks on before the other 469 are answered would put a red
job in CI on its first day, which is how a gate earns a reputation for being
ignorable.

## D-CORE-6: The timestamp fraction is rendered whole, then truncated

`to_string` built the fractional digits by dividing a place value that started at
1e8 nanoseconds and shrank by ten each digit. On the tenth digit that place value
is zero, so the expression was an integer division by zero.

`fractional_digits` is a public `std::uint8_t` on an aggregate, so 10 through 255
are all reachable by hand, and `D-CORE-1` is the reason the field exists at all:
the digit count is stored because it is the only way a canonical input
round-trips. `parse_timestamp` cannot produce a count above nine, which is why
this was never observed.

The fix is not a bound check. The nanosecond field is rendered as all nine of its
digits and then truncated to the requested width, which divides nothing, needs no
guard, and gives a defined answer for every value the type can hold. Truncation
rather than rounding keeps a shorter count a prefix of a longer one, so the same
instant never renders two ways depending on how many digits were asked for.

The type will stop admitting a count above nine when the attribute types land,
and this stays correct when it does.

## D-CORE-7: A wrong-branch read of the polyfill aborts rather than being undefined

`poly::expected<void, E>` stored a default-constructed `E` beside its flag, so
`error()` on a **successful** result returned `errc{0}`. That is not an
enumerator - `errc` starts at 1 - and `to_string_view` renders it `"unknown"`.
Under `std::expected` the same call is undefined.

So the two backends disagreed, and which one a build got was decided by the
standard library rather than by the call site. A polyfill may be smaller than
what it stands in for (ADR-0002); it may not behave differently.

The error moved into a union, as in the primary template, so there is no default
error to return. Reading either alternative when the other is held now calls a
function that is deliberately **not** `constexpr`: in a constant expression that
is a compile error naming what happened, and at run time it aborts.

That makes the polyfill stricter than `std::expected`, which is the useful
direction. `gcc-cxx20` and `polyfill-cxx23` are where a mistake surfaces, and
both are in CI. Aborting also keeps ADR-0001 intact: a precondition violation is
a defect in the caller, not a recoverable failure, so it is not something to
return.

## D-BIND-1: A prefixed datacontenttype is refused where it is read

Where a binding carries the media type in its own content-type field - HTTP and
Kafka both do, NATS does not - a `ce-datacontenttype` field matched no attribute
branch and fell through to the extension branch. The name is lowercase
alphanumeric, so it passed `valid_attribute_name`, and the reserved-name check
lives in `validate()` rather than there. The event was built carrying an
extension the encoder would refuse, and a caller who never called `validate()`
never found out.

`validate()` did refuse it, as `reserved_attribute_name`. That answer is true and
useless: the name is not the problem, the field is. The caller's fix is to send
the media type in the content-type field, and nothing in that error said so.

It is now refused in `read_attributes`, as `invalid_argument`, naming the
attribute and where the media type belongs. `invalid_argument` rather than
`reserved_attribute_name` because the complaint is about the field having no
place in this binding, which is the same complaint `to_message` already makes
when asked for a content mode a transport does not have.

Measured before changing it: no fixture under `test/fixtures/` contains
`ce-datacontenttype` or `ce_datacontenttype` in any spelling, and neither the Go
nor the Java golden emits one, so no peer sends this today.

## D-CORE-8: An event is built by a constructor, not by `create`

ADR-0008 first named the producer's entry point `event::create(id, source, type,
options)`. That name came from the draft in which construction was still expected
to fail. Construction cannot fail, because every argument is already a validated
type. A named factory returning a result is the idiom for construction that can
fail without exceptions, and the steps that can fail here already are factories:
each attribute's `make()` and `builder::build()`. The C++ Core Guidelines point
the same way (C.40, C.41): a class with an invariant gets a constructor that
leaves the object fully initialized. So the entry point is a constructor, and it
is `explicit`, so `return {...};` cannot hide which type it builds.

The three-argument form is a separate delegating constructor rather than a
default argument `options rest = {}`. `options` is nested in `event`, and its
default member initializers cannot be used before `event` is complete: CWG 1397
(C++11, `cpp/language/data_members#Defect_reports`) corrected the rule that had
treated the class as complete in its default member initializers, and a default
argument inside the class body is exactly such a premature use. GCC 16 rejects
it; the same default argument at namespace scope, in a test helper for example,
is fine.

## D-TIDY-2: The tidy gate is on, and it gates the headers only

The baseline D-TIDY-1 recorded was 469 findings. That number counted each header
finding once per suite that included the header. Counted once by location, with
the four checks D-TIDY-1 left off switched on as well, the real number was
**76**, measured 2026-09-23 with Homebrew LLVM 23.1.1.

They sorted the way D-TIDY-1 said they would:

| kind | count | answer |
|---|---|---|
| real | 5 | fixed: two `noexcept` functions that could throw, a missing move assignment on the copy-on-write `validated_string`, an uninitialised `ce::name`, an unchecked optional |
| clearer code satisfies it | 32 | restructured: index-based walks now consume their input (18), the four magic numbers are named (8, two checks each), and four over-complex functions are split into named steps (4) |
| mechanical | 29 | applied |
| wrong for this code | 10 | exempted at the site: `return {x};` on `nlohmann::json` builds a one-element array, its object `operator[]` inserts rather than indexes, and a string literal's type is a C array, so `ce::name`'s constructor and deduction guide must take one |

`readability-magic-numbers`, `cppcoreguidelines-avoid-magic-numbers`,
`cppcoreguidelines-pro-bounds-constant-array-index` and
`cppcoreguidelines-pro-bounds-pointer-arithmetic` are now on.

D-TIDY-1 also assumed `HeaderFilterRegex` kept the findings to
`include/cloudevents`. It does not: clang-tidy always reports the file it was
given, so linting the suites directly enforces the whole check set on the test
code too. The gate therefore runs through `cmake/tidy_gate.py`, which keeps the
findings located under `include/cloudevents/` and fails on any of them, or on a
suite that does not parse. Two alternatives were rejected. A translation unit
that only includes the headers would leave every class template uninstantiated,
which is most of this library. And a `test/.clang-tidy` that switched checks off
would switch them off for the headers as well, because a header finding is
judged by the configuration of the file that included it.

The CI job uses Homebrew's LLVM on macOS, not Ubuntu's packaged clang-tidy,
which is several releases older and reports a different set. A gate that
disagrees with what a contributor sees locally teaches people to ignore it. The
cost is that a new LLVM release can add findings on its own; when one does, they
take the same three-way sort, in their own change.

## D-DOCS-1: The guide's code is extracted and run, not copied into an example

`docs/GUIDE.md` is the only user documentation, so a snippet that no longer
compiles is the documentation lying. The `example_guide` test is generated from
the guide itself by `cmake/extract_guide_snippets.py`: every `cpp` block is
compiled, and every `cpp body` block also runs. A `#line` directive points a
compiler error at the guide's own line.

Keeping the snippets in `examples/` and pasting them into the guide was
rejected, because nothing would notice the two drifting apart. The cost is a
Python 3 interpreter wherever the examples are built, which the examples job,
the sanitizer job and the coverage job already have.

## D-CODE-1: Why the header code is shaped the way it is

The public headers carry no prose: only `// spec:` markers, `// TODO(#NN):`
markers, NOLINT directives and the namespace closers clang-format maintains.
The caller-facing contract is in `docs/GUIDE.md`. The reasons below were
comments in the headers and are recorded nowhere else. Each is a shape someone
tidying the code would plausibly undo.

| where | the shape | why |
|---|---|---|
| `core.hpp`, `detail::utf8` | payload masks are `std::uint32_t` | two `unsigned char` operands promote to `int`, so the code point would be assembled in signed arithmetic |
| `core.hpp`, `tagged_string` | implicit constructors from `std::string` and `const char*` | an exact `std::string` still selects the `std::string` alternative of `attribute_value`, because an exact match beats a user-defined conversion |
| `core.hpp`, `event::get` | the struct is filled field by field | the fields are reached through the describe seam, so no designated initializer can name them; the same holds in `from_json_value` |
| `detail/validated_string.hpp` | `owned_` empty means the text is borrowed | sound only because every policy refuses the empty string, so an owning instance never holds one |
| `detail/validated_string.hpp`, `detail/timestamp.hpp` | a bad literal calls a declared, undefined function | calling it in a constant expression is the error and its name is the message; a `throw` would break the no-exceptions preset |
| `detail/timestamp.hpp`, `parse_timestamp` | the range is checked in seconds, with one second of headroom | the overflow is silent once the value is widened to nanoseconds, and the headroom keeps the sub-second part from tipping it over |
| `detail/timestamp.hpp`, `parse_timestamp` | `fraction_digits::make` is called although the grammar admits at most nine digits | the bound belongs to the type, so a change to the pattern is reported rather than truncated |
| `detail/config.hpp` | reflection needs `__cpp_impl_reflection`, not `__has_include(<meta>)` | `<meta>` and `__cpp_expansion_statements` both exist at plain `-std=c++2c`, where reflection is off |
| `result.hpp`, `widen` | a free function, not a converting constructor | declaring any constructor on `error` would stop it being an aggregate, and every call site builds one with a designated initializer |
| `binding/detail/percent.hpp`, `percent_decode` | the byte is assembled unsigned | `hex_value` returns `int` to report "not a hex digit" as -1, and shifting a signed value into a byte's high bit is implementation-defined past `CHAR_MAX` |
| `binding/common.hpp`, `content_type_policy` | `content_type_is_attribute` is detected, not required | a traits type written before the flag existed still satisfies `binding_traits` and keeps its meaning |
| `binding/http.hpp`, `http::detail` | helpers are using-declarations, not wrappers | a using-declaration keeps `constexpr`, which the suites' `static_assert(ce::http::detail::needs_escape(...))` depends on |
| `binding/http.hpp`, `binding/kafka.hpp`, `detect_content_mode` | the batch prefix is tested first | `application/cloudevents-batch+json` also starts with `application/cloudevents`; tested second, a batch reaches the format layer and is reported as a parse error instead of a mode error |
| `binding/kafka.hpp`, `record` | the key sits beside the message rather than in it | `message` is shared by every binding and its shape is pinned by `SWR-HTTP-0001` |
| `format/json_format.hpp`, `read_event` | context attributes, then the payload, then the extensions, with `specversion` first | the order decides which problem a document with several is reported for, and a document from another version is named as such |
| `format/detail/json_slice.hpp`, `json_slicer::string_run_end` | eight bytes are loaded with `memcpy` only outside constant evaluation, and the exact byte is taken from the lowest flagged bit only on a little-endian target | `memcpy` is not `constexpr`, and the byte loop that follows gives the same answer; the lowest flagged byte is exact because a borrow only propagates upward from a byte that is itself flagged, while on a big-endian target the loop finds the byte instead |
| `format/detail/json_slice.hpp`, `json_slicer`, `batch_data_slices`, `data_member_text` | the slicer views the caller's input for one decode call; the constructors and `data_member_text` delete an rvalue `std::string` overload; `object_member` is private and views the slicer's input | a view of a temporary string dangles as soon as the full expression ends, and gcc 16 does not warn about it; a deleted overload turns the mistake into a compile error. The overload is a constrained template, so a string literal still converts to `std::string_view` rather than being ambiguous between the two |
| `format/json_format.hpp`, `json_payload` | `extract` is the decoder's last touch of the document, and runs only when `find` returns the very member that was claimed | every attribute and extension has been read by then, so nothing reads a moved-from member; with duplicate `data` keys the claimed member may not be the one `find` sees, and that case copies instead |
| `codec/nlohmann.hpp`, `as_int` | `is_number_unsigned` is tested before `is_number_integer` | the latter is also true for an unsigned value, which would make the range check dead code (D-JSON-3) |
| `codec/rapidjson.hpp`, `chars_of` | an empty view's pointer is replaced by `""` | RapidJSON asserts a non-null pointer, and the assertion is compiled out of a release build |
| `detail/describe_reflection.hpp` | a wire name is interned with `define_static_string` | an extracted annotation is a prvalue whose array a `string_view` would outlive |
| `core.hpp`, `json_document` | copy operations are declared `= default` and move operations are not declared | an rvalue then copies, which is one reference-count increment, so no moved-from document with an empty model can exist and every member may dereference it |
| `core.hpp`, `json_document` | codec identities are compared by value, never by the address of a tag | MSVC's default `/OPT:ICF` can give different read-only data one address, and a false match would make the downcast undefined behaviour (ADR-0010) |
| `core.hpp`, `json_document_holder` | `value_` is initialised with parentheses | braces on `nlohmann::json` select its `initializer_list` constructor and wrap the document in a one-element array |

## D-JSON-4: A `json_text` payload never equals a `json_document` payload

Two events that differ only in holding the same JSON value as text and as a
document compare unequal. `data_t` keeps its defaulted comparison, which
compares the alternative first.

The alternative, comparing text with a document by value, breaks
transitivity. `json_text` compares by its bytes, as it has since v0.1.0, so
`{"a":1}` and `{ "a" : 1 }` are two unequal texts. Both would equal the
document holding that value, and equality would stop being an equivalence
relation, which every container and algorithm that uses `==` assumes. Making
`json_text` compare by value instead would change a v0.4.0 behaviour and
require a parser in `core.hpp`.

The cost falls on a test or caller that builds an event from text and compares
it with a decoded one; the decoded payload is now a document. The v3 suites
compare against the same event with its payload as a document.

## D-JSON-5: `decode_options` is a plain aggregate beside the content types

`ce::json::decode_options{.retain_document_up_to = 16 * 1024}` sits in
`ce::v3::json`, next to `content_type`, and `decode` and `decode_batch` take it
as a defaulted last parameter. A plain aggregate is written in one designated
initializer at the call site, gains members without breaking a call, and keeps
`json_format<Codec>` a set of static functions with no state to configure. A
template parameter or a member of `json_format` was rejected: the limit is a
per-call policy, and a caller decoding trusted and untrusted input with one
codec needs both.

For a batch the limit is per event, by average size: a batch keeps documents
when its text is at most the limit times its number of events, and otherwise
every event in it keeps text. Measuring the whole batch text against the limit
sent a batch of 100 typical events to text and cost `decode_batch_100` up to 42
percent more allocated bytes than main. The average keeps retained memory
proportional to the input, with a worst case around 3 times its text: one large
event among tiny ones is retained because the average is small. The absolute
per-event bound applies to single-event decode. The product of the limit and
the event count is checked, so a huge count cannot wrap it to a small bound. The
owner chose this on 2026-09-25 (SWR-JSON-0040). The element events of a
batch move their payload out with `Codec::extract`, as a single decode does:
the batch decoder owns the array it parsed and walks it with
`for_each_mutable_element` (SWR-JSON-0039).

## D-JSON-6: `from_value` always keeps a document

`from_value(const value&)` keeps the payload as a `json_document`, copying the
member with `Codec::copy`, and takes no `decode_options`. It has no text to
measure, and its caller already holds the whole DOM in memory, so the limit's
purpose, bounding what a small hostile input can expand into, does not arise.
It cannot move the member out, since the DOM is const and remains the caller's.

## D-JSON-7: The payload's own text is found by a scanner that falls back when unsure

Above the retention limit, `decode` and `decode_batch` store the `data`
member's own bytes from the input as `json_text` (SWR-JSON-0043). The bytes are
found by `json_slicer` in `format/detail/json_slice.hpp`, which runs only after
the codec has parsed the input. It is linear, allocates nothing, tracks nesting
with a counter rather than recursion, and validates numbers with CTRE.

It is built for the text path's cost, since it reads every byte of a large
payload. A 256-entry table classifies each character outside strings, so the
nesting loop does one lookup per character. A string body is read eight bytes
at a time, testing each word at once for a quote, a backslash or a control
character, so escapes are validated only where a backslash occurs. On the
52,889-byte benchmark event this cut the scanner from 1,210,146 to 565,365
instructions per scan (g++-14, arm64 Valgrind), from about 23 to 11 per byte.

The scanner returns nothing, and the decoder stores `Codec::dump` of the member,
in each of these cases:

| case | why the slice is not trusted |
|---|---|
| any top-level member name containing an escape | an escape can spell `data` (`"d\u0061ta"`), and decoding names to compare them would need a buffer; the rule is deliberately wider than "spells data" |
| `data` at the top level more than once | codecs differ in which duplicate they keep: nlohmann the last, mini_codec the first |
| a trailing comma, a number outside JSON's grammar, a misspelt literal, a raw control character or an unknown escape in a string | a lenient codec may accept them, and the stored text must be JSON any codec reads |
| a byte order mark, content after the event, or anything else that is not an object where one is expected | the scanner cannot tell what the codec made of it |

In a batch the scanner decides per element: an element it is unsure of falls
back on its own, and the next one still gets its slice. A structural surprise
(the scanner losing its place) sends the rest of the batch to the fallback,
since the scanner can no longer line its elements up with the codec's.

The constants in the header come from RFC 8259: section 2 for the four
whitespace characters and the structural terminators, section 6 for the number
grammar, and section 7 for the escapes, the four hex digits of `\u`, the two
characters that introduce an escape, and `0x20`, the first character a string
may hold unescaped. The word constants follow from the word's type:
`every_byte_one` is the word's maximum divided by the byte's, and
`every_byte_high_bit` shifts it by one less than the bits in a byte.

## D-TIDY-5: The v2 copies are outside the clang-tidy gate

`include/cloudevents/v2/` holds what v0.4.0 published, copied for ADR-0010.
They were copied from headers the gate had passed, so today they carry no finding.
They stay outside it all the same: a check added later could only be satisfied by reshaping a generation that exists to stay as it was published, and `ce::v2` takes defect fixes, not clean-ups.

The mechanism is the one D-TIDY-4 uses.
The copied v0.4.0 suites are registered by `ce_add_v2_test`, which does not record itself for the gate, and no gated suite includes a `v2/` header.
That is why the check that `ce::v2::event` and `ce::event` are distinct types lives in `test/v2/v2_generation_test.cpp` and not in `test/build_test.cpp`.
The headers `ce::v2` shares with `ce::v3`, such as `attributes.hpp` and `message.hpp`, are included by gated suites and stay gated.

## D-TIDY-4: The v1 headers are outside the clang-tidy gate

`include/cloudevents/v1/` holds what v0.3.0 published, restored for ADR-0009.
v0.3.0 predates the tidy gate, and D-TIDY-2 measured 76 findings against that
code. Clearing them would mean reworking a generation whose whole purpose is to
stay as it was published; `ce::v1` takes defect fixes, not clean-ups.

The gate never saw them anyway: it lints the suites `ce_add_test` registers, and
the restored v1 suites are registered by `ce_add_v1_test`, which does not record
itself for the gate. So no v2 suite includes a v1 header, and no finding in one
can reach the gate. The shared headers are the exception that matters: they are
included by v2 suites and stay gated, which is why the restored describe seam
carries the suppressions in D-TIDY-3.

## D-TIDY-3: Why each NOLINT is there

A NOLINT stays in the header, because it is an instruction to the tool rather
than prose. Its reason is here.

| where | check suppressed | why |
|---|---|---|
| `detail/validated_string.hpp`, `literal(const char*)` | explicit-constructor | the implicit conversion from a literal is the literal path; the constructor is `consteval`, so it cannot accept anything it has not checked |
| `detail/validated_string.hpp`, `validated_string(literal)` | explicit-constructor | the only implicit entry point, reachable only from a value the compiler already accepted |
| `detail/expected_polyfill.hpp`, `expected(T)` and `expected(unexpected<E>)` | explicit-constructor | implicit, as `std::expected`'s are; `ce::fail()` converts into any `result<T>` through them |
| `core.hpp`, `tagged_string` (two constructors) | explicit-constructor | see D-CODE-1: the implicit conversion is what makes `ce::uri` usable as an `attribute_value` alternative |
| `detail/timestamp.hpp`, `fraction_digits(int)` | explicit-constructor | keeps `.fractional_digits = 3` working in a designated initializer; the constructor is `consteval` and refuses a count above nine |
| `describe.hpp`, `name` constructor and deduction guide | avoid-c-arrays | a string literal's type is a C array, and it is the only form from which `N` can be deduced |
| `describe.hpp`, `name::value` | avoid-c-arrays | the describe seam is shared with `ce::v1`, which published `char value[N]` in v0.3.0 and keeps that declaration (ADR-0009) |
| `describe.hpp`, `for_each_field` (both overloads) | missing-std-forward | v0.3.0 published the visitor as `F&&`, which `ce::v1` keeps; it is called once per member, so forwarding it would move from it more than once |
| `format/base64.hpp`, `base64_character` | pro-bounds-avoid-unchecked-container-access | the mask bounds the index, and a `static_assert` keeps the alphabet exactly as long as the mask allows |
| `binding/common.hpp`, `detail::text_of` | pro-type-reinterpret-cast | a structured or batched body is parsed through a `std::string_view` over the message's own bytes instead of a copy of them. Reading `std::byte` storage through `const char*` is defined because `char` may alias any object (`[basic.lval]`), both types are one byte, and the view only reads. It lives as long as the `message` it views, which the decode call holds by reference |
| `detail/describe_macro.hpp`, `field::name` | scudoai-copy-view-member | the name is a string literal the macro writes: the stringised member name, or the wire name `CE_FIELD` passes. A literal has static storage, so the view outlives every `field`. The header is shared with `ce::v1`; a NOLINT is a comment and keeps the declaration (ADR-0009) |
| `detail/validated_string.hpp`, `literal::text_` | scudoai-copy-view-member | both constructors are `consteval`, so the pointer is a constant expression, and a pointer in a constant expression can only point to an object with static storage duration |
| `detail/validated_string.hpp`, `validated_string::borrowed_` | scudoai-copy-view-member | it views either a `literal`'s static text or the object's own `owned_`. Each copy and move operation re-points it at the new object's `owned_` whenever that is not empty, and an owning instance is never empty because every policy refuses the empty string; the test "an owning instance is never empty" pins that |
| `result.hpp`, `static_error::detail` and `where` | scudoai-copy-view-member | the library fills them only with string literals and with attribute names that are themselves literals, and `widen` copies both into an owning `error` before a diagnosis leaves the check that made it. The header is shared with `ce::v1`; a NOLINT is a comment and keeps the declaration (ADR-0009) |
| `format/detail/json_slice.hpp`, `json_slicer::text_`, `object_member::name` and `value` | scudoai-copy-view-member | each view borrows the input of one decode call, which outlives the slicer; see D-CODE-1 for how a temporary is refused |
| `format/detail/json_slice.hpp`, `json_slicer::at` | pro-bounds-avoid-unchecked-container-access | the index is compared with the size on the same line, and past the end `at` returns `'\0'`, which every caller treats as a character it does not accept |
| `format/detail/json_slice.hpp`, `class_of` | pro-bounds-avoid-unchecked-container-access, pro-bounds-constant-array-index | the index is an `unsigned char`, and the table has one entry for every value it can hold |
| `codec/nlohmann.hpp`, the value constructors | return-braced-init-list | `return {x};` on `nlohmann::json` selects its `initializer_list` constructor and builds a one-element array |
| `codec/nlohmann.hpp`, `set` | pro-bounds-avoid-unchecked-container-access | on an object, `operator[]` inserts or replaces a member; there is no index to check |
| `core.hpp`, `json_document` | special-member-functions | see D-CODE-1: declaring no move operations is what keeps a document from ever being empty |
| `core.hpp`, `json_document::get` and `json_document_holder::equal_value` | pro-type-static-cast-downcast | the declared codec identities have already compared equal, so the model is that codec's holder; the identity is the codec's declared contract (SWR-CORE-0034), and RTTI was declined by the owner (ADR-0010) |
