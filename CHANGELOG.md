# Changelog

Notable changes per release. Dates are the tag date.

## Unreleased

A third API generation, `ce::v3`, is the inline namespace.
It carries the CR-0003 changes listed below.
`ce::v2` keeps what v0.4.0 published, so v0.4.0 code has a way to keep compiling.

### Three generations

- **`ce::v3` is the inline namespace**, so `ce::event` now names the v3 class.
- **`ce::v2` holds the v0.4.0 surface.**
  The headers that mention `event` or `data_t` are copied under `include/cloudevents/v2/`: `core.hpp`, `format/json_format.hpp`, `format/typed_payload.hpp` and the four binding headers.
  A v2 declaration does not change; v2 takes defect fixes only.
- **The attribute types are shared by v2 and v3**: `id`, `source`, `timestamp`, `message`, the literals, the typed extensions and the rest of `attributes.hpp` are declared once, in `ce::v2`, and are one type through `ce::`, `ce::v2::` and `ce::v3::`.
- **What v0.3.0 already shared stays shared**: `errc`, `error`, `result`, the three codecs, base64 and the describe seam are one type through all three generations.
- **v3 codecs provide `equal`, `copy`, `extract`, `for_each_mutable_element` and `identity`.**
  `ce::v3::json::json_codec` adds five members to the v1 concept: `C::equal(const value&, const value&) -> bool`, JSON value equality with object members in any order; `C::copy(const value&) -> value`, a deep copy; `C::extract(value& object, std::string_view key) -> value`, which moves a member out of an object and returns null for a key `find` would not return; `C::for_each_mutable_element(value& array, F visit)`, which hands each element to `visit` as a `value&`; and `C::identity`, a non-empty `static constexpr std::string_view` naming the codec.
  The three in-tree codecs gain all five, and keep their v1 declarations; their identities are `io.cloudevents.cpp.codec.nlohmann`, `io.cloudevents.cpp.codec.boost_json` and `io.cloudevents.cpp.codec.rapidjson`.
  A third-party codec must add them to serve `ce::v3`, with an identity that is a reverse-DNS name under a domain its author controls and that no other codec uses.
  It keeps serving `ce::v1` and `ce::v2` unchanged, since their concept is the v1 one.
- **`ce::json_document` holds a parsed JSON document** in `core.hpp`, which still names no codec type.
  `json_document::make<Codec>(dom)` builds one, `get<Codec>()` returns the DOM to a codec declaring the identity of the one that built it and `nullptr` to any other, and `dump()` serialises it.
  Documents compare as JSON values, across codecs too.
  Copies share one immutable DOM, so a document can be copied, compared and read from several threads at once.
  A document is never empty: it has no default constructor, and moving one copies it.
- **`data_t` has a fifth alternative, `json_document`.**
  Code that visits `data_t` exhaustively stops compiling against `ce::v3` until it handles it; that is the migration.
  An event holding `json_text` never equals one holding `json_document`, even for the same JSON value, so equality stays transitive.
- **A structured decode keeps the payload as a `json_document`** built by the decoding codec, where v2 gave `json_text`.
  `decode` and `decode_batch` move the `data` member out of the document they parsed, so the payload is neither serialised nor copied; `from_value` copies it.
  A binary-mode body, a non-JSON string payload and `data_base64` decode as before.
- **`ce::json::decode_options{.retain_document_up_to = 16 * 1024}`** bounds what one event can pin: input longer than the limit keeps its payload as `json_text`, and 0 always does.
  The default is 16 KiB, named `decode_options::default_retention_limit`: a retained document measured up to 3.3 times the bytes of its text, so larger payloads stay text unless the caller raises the limit.
  `decode` and `decode_batch` take it. A batch keeps documents when its text is at most the limit times its number of events, and otherwise every event in it keeps text.
- **A payload kept as text above the limit is the input's own text** of the `data` member, without the whitespace around it, rather than the codec's serialisation of it.
  Copying the slice avoids the serialisation, which cost `decode_large` 25 to 49 percent more instructions and 58 to 210 percent more allocated bytes than main.
  The decoder falls back to the codec's serialisation when a top-level member name carries an escape, when `data` appears twice, or when the input holds something strict JSON does not allow; a batch decides per element.
  The text keeps the sender's spelling, so the same JSON formatted differently no longer compares equal as `json_text` after decode.
- **Encoding a `json_document` built by the encoding codec copies its DOM**, with no serialisation and no parse.
  A document from another codec converts through the building codec's text.
- **`data_as<T, Codec>` reads a document built by `Codec` directly**, through `get<Codec>()`, with no serialisation and no parse.
  A document from another codec still converts through text.
- **`set_data<T, Codec>` stores a `json_document`** built by `Codec`, where v2 stored `json_text`.
  An event written by `set_data` therefore equals the same payload held as a document, never as text.
- **Typed entry points parse and write once.**
  `ce::decode_as<T, Codec>(text, options)` returns `ce::decoded<T>{.event, .payload}`: the event `decode` returns, and the payload read from the parsed `data` member.
  `ce::decode_batch_as` does the same for a batch and fails the whole batch when one payload does not read as `T`; `ce::from_value_as` reads a document the caller parsed.
  `ce::encode_as<T, Codec>(event, payload)` writes the payload's DOM under `data` without changing the event, writing `application/json` when `datacontenttype` is absent and refusing a non-JSON one with `type_mismatch`.
  They fail as `data_as` does when the payload is absent (`missing_required_attribute`) or is not JSON (`type_mismatch`).
  The optional module exports them, and `json::decode_options`.
- The optional module exports `ce::v3` only.

#### Moving to the v3 payload

- Add a `json_document` case wherever you visit `data_t`; `document.dump()` gives its compact text.
- Code that read `std::get<ce::json_text>(event.data()).raw` after a structured decode now finds a `json_document`.
  Read it with `document.get<Codec>()` for the DOM, or `document.dump()` for text.
- Code that compares a decoded event with one built from `json_text` now compares unequal; build the expected event with a `json_document`, or compare the payloads by value.
- Code that read `std::get<ce::json_text>(event.data())` after `set_data` now finds a `json_document`; read the payload with `data_as`, or the text with `dump()`.
- Replace `decode` followed by `data_as` with `decode_as`, and `set_data` on a copy followed by `encode` with `encode_as`, to save a parse or a serialisation.
- Pass `{.retain_document_up_to = 0}` to keep the v2 behaviour of always decoding to text. The text is now the sender's own spelling of the payload, where v2 gave the codec's serialisation.

CR-0003 and ADR-0010 record why: the v0.5.0 event model adds an alternative to `data_t`, and SPEC section 3 rule 4 sends a breaking change to a new generation.

### Staying on v0.4.0 code

`ce::X` is now the v3 entity.
To keep the v0.4.0 declarations, spell `ce::v2::X` and include the `v2/` copy of each header that has one:

```cpp
#include <cloudevents/v2/binding/http.hpp>

using namespace ce::v2::literals;
auto order = ce::v2::event::builder{.id = "A1"_id, .source = "/orders"_source,
                                    .type = "com.example.placed"_type}.build();
auto request = ce::v2::http::to_message<ce::v2::codec::nlohmann_codec>(
    *order, ce::v2::content_mode::binary_mode);
```

Code that names only shared entities, such as an attribute type or a codec, needs no change.

### Tooling

- **Every pull request is measured against main and against budgets.**
  The `perf` workflow gates on instructions under Callgrind, heap allocations and the bytes a decoded event retains, per operation and codec, and comments the table on the pull request.
  Wall time and binary size are reported.
  Each merge to main is recorded on the `bench-data` branch.
  [docs/PERFORMANCE.md](docs/PERFORMANCE.md) explains the table and how to accept a deliberate cost.
- The bench's Boost.JSON codec builds its values with parentheses, so it works with Boost before 1.84.

## v0.4.0

A second API generation, `ce::v2`, in which an invalid CloudEvent cannot be
written down. `ce::v1` keeps what v0.3.0 published, so existing code has a way
to keep compiling.

### Two generations

- **`ce::v2` is the inline namespace**, so `ce::event` now names the v2 class.
- **`ce::v1` holds the v0.3.0 surface**, under `include/cloudevents/v1/`: the
  aggregate `event`, `validate()`, `headers`, and the format and bindings built
  on them. A v1 declaration does not change; v1 takes defect fixes only.
- **What did not change is shared**: `errc`, `error`, `result`, the codec
  concept, the three codecs, base64 and the describe seam are one type through
  `ce::`, `ce::v1::` and `ce::v2::`. A codec written for v0.3.0 serves both.
- The optional module exports `ce::v2` only.

CR-0002 and ADR-0009 record why: SPEC section 3 rule 4 sends breaking changes to
a new generation, and v0.4.0's event model is one.

### Staying on v0.3.0 code

Spell `ce::v1::` and include the `v1/` headers. Nothing else changes:

```cpp
#include <cloudevents/v1/binding/http.hpp>

const ce::v1::event order{.id = "A1", .source = "/orders", .type = "com.example.placed"};
auto request = ce::v1::http::to_message<ce::v1::codec::nlohmann_codec>(
    order, ce::v1::content_mode::binary_mode);
```

### Moving to `ce::v2`

| v0.3.0 (`ce::v1`) | v0.4.0 (`ce::v2`) |
|---|---|
| `ce::event{.id = "1", .source = "/s", .type = "t", .subject = "x"}` | `ce::event{"1"_id, "/s"_source, "t"_type, {.subject = "x"_subject}}` |
| a runtime string assigned to a member | `ce::id::make(text)` and its siblings, which return a `result` |
| `if (auto ok = e.validate(); !ok)` | nothing to call: construction refuses what `validate()` refused |
| `e.id`, `e.subject` | `e.id()`, `e.subject()`, returning the validated types |
| `e.set_extension("name", value)` returning `result<void>` | `e.set_extension("name"_ext, value)`, which cannot fail |
| `e.datacontenttype = ...; e.data = ...;` | `e.set_data(payload, "application/json"_mediatype)` |
| `ce::headers` | `ce::raw_headers`; `ce::headers` is now the type that refuses a repeated field |
| `binding::read_attributes` returning an `event` | returns an `event::builder`; `build()` reports a missing attribute |
| `binding::read_body(body, event&)` | `read_body(body, media_type)` returns the payload |
| `ce::set_data<T, Codec>` returning `result<void>` | returns nothing, because it cannot fail |
| `timestamp::fractional_digits` as `std::uint8_t` | a `fraction_digits`, refusing a count above nine |

A literal is checked when the program compiles: `""_id` does not build.
`docs/GUIDE.md` covers the whole v2 surface.

### Fixed, in both generations

- A timestamp with more than nine fractional digits divided by zero when
  rendered. v2 cannot hold one; v1 renders nine.
- The RapidJSON codec handed a null pointer to RapidJSON for an empty string.
- The `result<void>` polyfill answered `error()` on a success with `errc{0}`
  where `std::expected` leaves it undefined; a wrong-branch read now aborts.
- base64 accepted any run of padding, so `"QQ======"` decoded.
- A message carrying one `ce-` attribute twice decoded to whichever came last.
  It is now refused with `invalid_argument`.
- A prefixed `datacontenttype` on HTTP or Kafka was accepted as an extension and
  then refused as a reserved name. It is now refused as `invalid_argument`.
- The HTTP percent-encoding policy now refuses ill-formed UTF-8 on the way out,
  as it always did on the way in.

### Added

- `docs/GUIDE.md`, the user guide. Every C++ block in it is compiled and run in
  CI. `CONTRIBUTING.md` and `SECURITY.md`.
- A clang-tidy gate over the public headers, including the magic-number checks.
- `check_references.py` checks the `// spec:` markers in the headers against
  each requirement's `satisfied_by`, in both directions.

### Removed

- The Doxygen reference. The headers carry `// spec:` markers instead of doc
  comments, and the guide is the documentation.

### Measured on the release candidate

- Line coverage 95.8% (2416 of 2522), function coverage 92.5%, branch coverage
  60.3%, against a floor of 90% lines.
- CI: GCC 13 at C++20 and C++23, Clang 16 with libstdc++ 13, Clang 17 with
  libc++ 17, AppleClang at C++20 and C++23, MSVC 19, GCC 16 with C++26 static
  reflection, plus the forced polyfill, exceptions disabled, no default codec,
  every codec on Linux and macOS, ASan with UBSan, and install-and-consume.
- 45 CTest entries per preset: 27 for `ce::v2` and 18 for `ce::v1`, of which
  16 are the v0.3.0 suites unchanged.

### Known limitations

- libc++ 17 and 18 implement `std::format` without defining `__cpp_lib_format`;
  the SDK carves them out by version (D-CI-1).
- Clang 16 cannot compile libstdc++ 13 or 14 in C++23 mode, so the C++23 Clang
  job uses libc++ (D-CI-1).
- The module interface is usable, with constraints on what an importing
  translation unit may also include (D-MODULE-1), and it exports `ce::v2` only.
- Given the same non-JSON payload, the Java SDK writes `data_base64` where Go
  and this SDK write a JSON string. Both are legal (D-INTEROP-1).
- HTTP binary mode percent-encodes header values as the binding requires; the Go
  and Java SDKs do not, so talking to them needs `ce::http::literal_values`
  (D-HTTP-1).
- A `ce::v1::timestamp` can still be given more than nine fractional digits by
  hand. The renderer is guarded; the type is v0.3.0's and does not change.

## v0.3.0

NATS binary mode, and the reason it was missing.

### Added

- **`ce::nats::to_message` / `from_message`.** Binary mode maps each attribute
  to a header named for it with a `ce-` prefix, percent-encoded by HTTP's rule.
  It needs a server at NATS 2.2 or later, which is where the protocol gained
  headers. All three bindings now support binary mode.
- `ce::nats::detect_content_mode`. The binding inverts HTTP's default: a
  CloudEvents content type means structured and **anything else means binary**,
  including no content type at all.

`to_payload` and `from_payload` are unchanged and not deprecated. They remain
the right API for a server before 2.2, which cannot carry a header (D-NATS-2).

### Two rules that differ from every other binding here

- **datacontenttype is not special.** It maps to `ce-datacontenttype` like any
  other attribute, and `Content-Type` is left to mean that a message is
  structured. The shared binding core gained `content_type_is_attribute` for
  this, **detected rather than required**, so a `binding_traits` type written
  before it still satisfies the concept unchanged.
- **No batch mode**, which the binding does not define.

### Why this was missing

`SWR-NATS-0001` forbade binary mode, quoting the v1.0.2 binding: "the NATS
protocol does not support custom message headers, necessary for binary mode".
NATS 2.2 introduced headers in 2021 and the binding now says "Every compliant
implementation SHOULD support both structured and binary modes".

The requirement was written against a frozen tag and the tag moved. Nothing in
the gates would catch that: they check a requirement is traceable and verified,
not that the document it was derived from still says the same thing.

### Following the specification rather than the only implementation

sdk-go's `protocol/nats_jetstream` is the only other header-based NATS mapping
and differs from the binding on three rules: `content-type` unprefixed instead
of `ce-datacontenttype`, binary detected by `ce-specversion` presence rather
than content-type absence, and no percent-encoding where the binding requires
it. Reported as `cloudevents/sdk-go#1334`. This SDK follows the binding, and
deliberately ships no compatibility mode for the divergence: no other SDK has a
NATS binary mode yet, so there is nothing to be compatible with (D-NATS-1).

### JetStream

There is no JetStream binding in the CloudEvents specification, so there is
nothing named to implement. JetStream carries the same subjects, headers and
payloads as core NATS; persisting and delivering them is the client's, as
moving any message onto a wire is.

### Known cost

Binary mode follows the binding on `main`, marked 1.0.3-wip, while the rest of
the SDK implements v1.0.2. If it changes before 1.0.3 is cut, this follows it.

## v0.2.0

Three more JSON codecs to choose between, two more protocol bindings, and the
cross-SDK interoperability testing that found a defect in the first one.

Everything is additive. `ce::v1` gained entities and lost none, so
`SWR-BUILD-0006` holds and existing code compiles unchanged.

### Codecs

- `ce::codec::rapidjson_codec` and `ce::codec::boost_json_codec` beside the
  nlohmann one (`ce::codec_rapidjson`, `ce::codec_boost_json`).
- `CE_CODECS` replaces the singular `CE_DEFAULT_CODEC`, which still works and
  now subtracts from the list. A build may enable any set, or none.
- `README.md` says which to choose, from what `bench/` measured rather than
  from preference. Boost.JSON needs exceptions and the build refuses it under
  `-fno-exceptions` at configure time rather than failing at link.

### Bindings

- **Kafka** (`ce::kafka`): binary and structured mode, `ce_` prefix, byte-exact
  record header keys, UTF-8 values with no escaping, and `to_record` with an
  opt-in `partitionkey_mapper`. No batch mode, which the binding spec does not
  define (`D-KAFKA-1`).
- **NATS** (`ce::nats`): structured mode and the JSON event format, which is all
  the protocol supports. No subject is derived, and `not_a_cloudevent` is not
  reachable there (`SWR-NATS-0004`).
- `ce::binding` holds what the three share, parameterised by a `binding_traits`
  type, so a fourth binding is its traits plus its own entry points (ADR-0007).

### HTTP interoperability

- `ce::http::literal_values`, an opt-in value policy for talking to the Go and
  Java SDKs. **The default is unchanged** and still percent-encodes as the
  binding spec requires.
- Measured on 2026-09-21: sdk-go v2.15.2 and sdk-java neither encode nor decode
  header values, so a conformant sender is silently misread by both. sdk-csharp
  does implement the rule. `docs/DECISIONS.md` D-HTTP-1 has the tables;
  `cloudevents/spec#1397` has the cross-SDK picture.
- `literal_values` refuses a control character rather than passing one through:
  percent-encoding was also what kept CR and LF out of a header field.

### Core

- `headers::find_exact`, `contains_exact` and `set_exact`, byte-exact beside the
  case-insensitive originals. `set` erases case-insensitively, which is right
  for HTTP and wrong for every other transport.
- `ce::to_bytes` and `ce::to_text` are public, having been reached for from
  `ce::http::detail` by examples and suites already.
- `nlohmann_codec::as_int` reports `out_of_range` for an unsigned integer past
  `INT64_MAX` instead of reinterpreting it. `2^63` used to arrive as a negative
  `int32` that the format layer accepted.

### Testing

- Fuzz targets for the Kafka and NATS decode paths, seven in all. Pull requests
  run a one-minute budget per target in parallel, about two minutes of wall
  clock rather than the previous fifty-five; the nightly `fuzz` workflow runs
  fifteen minutes per target against a corpus that accumulates.
- `interop/audit/` runs binding-level cases through the Go SDK and commits what
  it observed, so an SDK changing its mind shows up as a diff. Of 21 cases, 14
  agree; the four that differ are listed in `interop/audit/README.md`.
- HTTP binary-mode header sets recorded from the Go SDK are committed and
  asserted against, which is the layer the percent-encoding defect lived in and
  the layer nothing had compared before.
- `test/json_format_checks.hpp` holds the format rules the suite and the
  benchmark both run, so a codec that is only measured is judged by the same
  rules as one that ships.

### Known limitation carried forward

An event that round-trips through the Go SDK comes back with its `time`
rewritten to UTC. The instant survives, the offset text does not, and
`ce::timestamp` compares by value -- so the same moment can compare unequal.

## v0.1.0

First release. Core event model, the JSON event format over a pluggable codec,
the HTTP protocol binding, typed extensions and payloads, the reflection seam
with a C++20 macro backend and a C++26 static reflection backend, CMake
packaging, and the traceability gates.
