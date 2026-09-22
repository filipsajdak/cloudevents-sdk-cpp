# cloudevents-cpp — work specification

Status: approved for implementation. Version 0.1 scope. Audience: Claude Code agent.

## 1. Goal and scope

Build a C++ SDK for CloudEvents v1.0.2. No official C++ SDK exists, so interoperability
with the other SDKs (Go, Java, Rust, C#) is the measure of correctness.

**In scope for v0.1**
- Core event model, attribute type system, validation
- Reflection seam with two backends (C++20 macro, C++26 static reflection)
- JSON event format: structured and batch, via a pluggable codec (default nlohmann/json)
- HTTP protocol binding: binary, structured, batched content modes
- Typed payloads and typed extension structs driven by the reflection seam
- CMake packaging, CI matrix, conformance tests, fuzz targets

**Delivered after v0.1**, on the shared binding core (ADR-0007)
- Kafka and NATS protocol bindings
- RapidJSON and Boost.JSON codecs

**Out of scope** (design must not block them)
- MQTT, AMQP, WebSocket bindings
- Protobuf, Avro, XML formats
- CloudEvents SQL, Subscriptions, Discovery, CESQL
- Any transport adapter doing real I/O (Beast, libcurl, Paho)

**Non-goals**: a JSON parser of our own; async runtime; schema validation of `data`.

## 2. Normative sources

Read the relevant document before implementing each part. Pin to tag `v1.0.2`.

- Core: `github.com/cloudevents/spec/blob/v1.0.2/cloudevents/spec.md`
- JSON format: `.../cloudevents/formats/json-format.md`
- HTTP binding: `.../cloudevents/bindings/http-protocol-binding.md`
- Documented extensions: `.../cloudevents/extensions/` (distributed tracing,
  partitioning, sequence, sampled rate, dataref)
- RFC 3339 (timestamps), RFC 3986 (URI), RFC 4648 §4 (base64), RFC 2046 (media types)

Requirement keywords MUST/SHOULD/MAY in those documents carry over. Every MUST gets
at least one test whose name cites the section, e.g. `"json-format §2.2 integer"_test`.

## 3. Language-version strategy

| Standard | Role | Requirements |
|---|---|---|
| C++20 | Floor | Full functionality. `result<T>` polyfill. `CE_DESCRIBE` backend. |
| C++23 | Supported | `result<T>` becomes `std::expected`. No API difference. |
| C++26 | Supported | Static reflection backend replaces the macro. Annotations for renames. |
| C++29 | Future | Must be a recompile, not a port. See rules below. |

Rules that keep the C++29 upgrade cheap:
1. Feature-test macros only, all in `detail/config.hpp`, each exposed as a
   `CE_HAS_*` constant. No other header tests compiler or standard version.
2. Before writing the reflection backend, **verify the exact feature-test macro names
   and values** (`__cpp_impl_reflection`, `__cpp_lib_reflection`, annotations,
   expansion statements) against the installed toolchain's documentation. Do not
   rely on memory; record findings in `docs/DECISIONS.md`.
3. Each polyfill mirrors the std surface exactly, so deleting it is a no-op for users.
4. Inline namespace `ce::v1`. Breaking changes go to `v2`, never mutate `v1`.
5. No deprecated or removed-in-C++26 library features. Deprecation warnings are errors.
6. CI carries an allowed-to-fail job building with the newest available
   `-std=` flag on compiler trunk to surface breakage early.
7. Headers are module-ready: no anonymous-namespace entities in headers, no
   macros leaking except `CE_DESCRIBE`. Ship an optional `cloudevents.cppm`
   wrapper that `export import`s the headers; it is not part of the default build.

## 4. Architecture

```
ce::core            std + CTRE           event, types, validation, describe seam
ce::format_json     core                 json_codec concept, json_format<Codec>
ce::codec_nlohmann  format_json+nlohmann nlohmann_codec (the default)
ce::codec_rapidjson format_json+RapidJSON rapidjson_codec
ce::codec_boost_json format_json+Boost.JSON boost_json_codec
ce::binding_http    core (+format)       message, http binding
ce::binding_kafka   core (+format)       kafka binding (binary and structured)
ce::binding_nats    core (+format)       nats binding (binary and structured)
ce::module          core                 optional cloudevents.cppm, off by default
```

All targets are header-only INTERFACE libraries. Dependency direction is strictly
downward; `core` never includes format or binding headers. `CE_CODECS` is the list
of codec targets to build (default `nlohmann`); `CE_DEFAULT_CODEC=OFF` subtracts
nlohmann from it and is kept because four requirements and a preset name it.

Layout:
```
include/cloudevents/{core,result,describe,message,extensions}.hpp
include/cloudevents/cloudevents.cppm
include/cloudevents/detail/{config,timestamp,expected_polyfill}.hpp
include/cloudevents/detail/{describe_macro,describe_reflection}.hpp
include/cloudevents/format/{json_codec,json_format,base64}.hpp
include/cloudevents/format/{typed_payload,describe_json}.hpp
include/cloudevents/codec/{nlohmann,rapidjson,boost_json}.hpp
include/cloudevents/binding/{common,http,kafka,nats}.hpp
include/cloudevents/binding/detail/percent.hpp
test/  fuzz/  examples/  bench/  interop/  cmake/  docs/  spec/
```

The five documented extensions are one `extensions.hpp`, not a directory: each is a
handful of fields and a `CE_DESCRIBE`, and splitting them would make a consumer include
five headers to read one event.

## 5. Component contracts

### 5.1 Core (`core.hpp`)

- `errc` enum and `error{errc code; std::string detail;}`.
- `result<T>`: alias of `std::expected<T, error>` when available, else a polyfill
  exposing `has_value`, `operator bool`, `operator*`, `operator->`, `error()`, plus
  `result<void>`. Helper `fail(errc, detail)`.
- Types: `binary = std::vector<std::byte>`, `uri`, `uri_ref`, `timestamp`,
  `attribute_value = variant<bool, int32_t, string, binary, uri, uri_ref, timestamp>`.
- `timestamp`: UTC instant (`sys_time<nanoseconds>`) plus original offset (`minutes`).
  `parse` via one CTRE pattern; `to_string` round-trips byte-for-byte for canonical
  input. Accept lowercase `t`/`z` and second `60` on parse. Do not use
  `std::chrono::parse`.
- `data_t = variant<monostate, std::string, binary, json_text>`; `json_text` holds
  serialized JSON so core stays codec-free.
- `event`: a class with no public data members (see §9 D1, reversed by CR-0001). It is
  constructed from already-validated `id`, `source` and `type` plus an `options`
  aggregate for `datacontenttype`, `dataschema`, `subject`, `time`, `extensions` and
  `data`, or by a decoder through `event::builder`. `specversion` is the single value
  `1.0`. Extensions are a `std::map<extension_name, attribute_value, less<>>`.
- Each context attribute is its own type, and each type refuses what the core
  specification forbids: required attributes empty, a `specversion` other than `1.0`,
  optional strings empty when present, extension names outside `[a-z0-9]+` or
  reserved. There is no `validate`. Name length over 20 is accepted (spec says SHOULD)
  and surfaced via a separate `lint()` returning warnings.
- `constexpr` validators: `valid_attribute_name`, `reserved_name`,
  `is_json_content_type` (`*/json`, `*/*+json`, case-insensitive, parameters allowed).
- Leniency principle: strict on produce, tolerant on consume. `source` is checked
  non-empty only; full RFC 3986 validation is not required.

### 5.2 Reflection seam (`describe.hpp`)

One customization point, consumed by JSON payload mapping, typed extensions and
header mapping. Consumers must never branch on the backend.

- `ce::described<T>` concept.
- `ce::for_each_field(obj, f)` calling `f(std::string_view name, member&)` in
  declaration order, for const and non-const objects.
- `ce::field_count<T>`, `ce::field_names<T>()` as `constexpr`.
- **C++20 backend**: `CE_DESCRIBE(Type, member...)` at namespace scope, generating a
  tuple of name/member-pointer pairs. Up to 32 members. Optional rename form
  `CE_FIELD(member, "wire_name")`.
- **C++26 backend**: `nonstatic_data_members_of` with an explicit access context,
  names from `identifier_of`, renames and skips via annotations
  (`[[=ce::name("x")]]`, `[[=ce::skip]]`). Public members only. A type that also has
  `CE_DESCRIBE` stays valid; the macro result takes precedence so behaviour cannot
  change silently on upgrade.
- Supported member types: `bool`, integral, floating point, `std::string`,
  `std::optional<T>`, `std::vector<T>`, `std::map<std::string, T>`, nested described
  types, and the core attribute types. Anything else is a `static_assert` with a
  readable message.
- **Parity contract**: one shared ut suite runs against both backends and must produce
  identical names, order and serialized output.

### 5.3 JSON codec and format

- `json_codec` concept abstracts a JSON **DOM**, not the event: `value` type,
  `parse(string_view) -> result<value>`, `dump(value) -> string`, constructors for
  null/bool/int64/double/string/array/object, `set`, `push`, `find`, `for_each_member`,
  `for_each_element`, kind inspection, and `as_*` accessors returning `result`.
  Exact signatures are yours to finalise; keep the surface minimal and document it.
- `nlohmann_codec`: non-throwing parse (`allow_exceptions=false`), map failures to
  `errc::parse_error`. No nlohmann type appears in any other header.
- `test::mini_codec`: a second, tiny in-tree codec used only by tests to prove the
  concept is not nlohmann-shaped. It may be slow and incomplete beyond what tests need.
- `json_format<Codec>`: `encode(event)`, `decode(string_view)`, `encode_batch(span)`,
  `decode_batch(string_view)`. Rules to implement exactly per the JSON format spec:
  - Integer as JSON number within int32; out-of-range or fractional is an error.
  - Binary attribute values as base64 strings; URI, URI-ref, Timestamp as strings.
  - `json_text` data emitted as a JSON value under `data`; `binary` under
    `data_base64`; `std::string` under `data` as a JSON string.
  - On decode: `data_base64` yields `binary`; `data` yields `json_text` unless
    `datacontenttype` is present and non-JSON and the value is a string, in which
    case it yields `std::string`. Both members present is an error.
  - Unknown top-level members become extensions. Because JSON loses the attribute
    type, decode extensions as: bool to `bool`, integral number to `int32_t`,
    string to `std::string`. Document this; typed extension structs (§5.5) recover
    richer types.
  - Media types: `application/cloudevents+json`, `application/cloudevents-batch+json`.
  - Empty batch `[]` is valid.
- `base64.hpp`: RFC 4648 §4 encode/decode, `constexpr`, rejects invalid input,
  accepts missing padding on decode.
- `event::data_as<T>(codec)` and `event::set_data(const T&, codec)` for described
  `T`, implemented in the format layer as free functions if that keeps core clean.

### 5.4 Message, the binding core, and the HTTP binding

- `binding::binding_traits`: what one protocol contributes. `attribute_prefix`,
  `content_type_header`, `case_sensitive_names`, `encode_value`, `decode_value`.
  The shared `write_attributes`, `read_attributes`, `write_body`, `read_body`,
  `render_attribute`, `encode_structured` and `decode_structured` are templates over
  it (ADR-0007). `case_sensitive_names` decides prefix matching, name
  normalisation, content-type lookup and replace-on-write together, so the read and
  write halves cannot disagree.
- `message{ headers, body }`: `headers` is an ordered multimap with case-insensitive
  lookup; `body` is `binary`. No HTTP library types.
- `http::to_message(event, mode, codec)` and `http::from_message(message, codec)`;
  batched variants taking and returning a vector.
- Content mode detection on receive, by `Content-Type`:
  `application/cloudevents-batch` prefix is batched, `application/cloudevents`
  prefix is structured, anything else is binary.
- Binary mode: attributes as `ce-<name>` headers; `datacontenttype` maps to
  `Content-Type` and must **not** also appear as `ce-datacontenttype`; body is the
  raw data. Header values are percent-encoded per the binding spec (space, double
  quote, percent, and everything outside printable ASCII, as UTF-8 bytes); decode
  must accept any percent-encoded octet. Invalid UTF-8 after decoding is an error.
- Binary-mode receive cannot know extension types: decode as `std::string`.
- A request with no `ce-specversion` and a non-CloudEvents content type is
  `errc::not_a_cloudevent`, distinct from malformed.

### 5.4.1 Kafka binding

- `kafka::to_message` / `from_message`, and `to_record` returning
  `record{ message value; std::optional<std::string> key; }`.
- Prefix `ce_`; `content-type` carries datacontenttype and takes no prefix.
- Header keys and values are UTF-8 strings with **no** escaping. A value that is
  not well-formed UTF-8 is `errc::invalid_utf8`, refused rather than transmitted.
- Header keys compare byte for byte (D-KAFKA-2).
- No batch mode in either direction (D-KAFKA-1); the batch content type is
  recognised before the structured one so the refusal names the mode.
- `key_mapper` concept; `no_key_mapper` is the default and `partitionkey_mapper`
  is the opt-in the binding spec asks for. The extension still travels as a
  header when it becomes the key.

### 5.4.2 NATS binding

- `nats::to_payload` / `from_payload` exchange structured mode as UTF-8 JSON
  text, which is all a server before NATS 2.2 can carry.
- `nats::to_message` / `from_message` are the general form. Binary mode needs
  NATS 2.2, which introduced message headers; the binding spec now says every
  implementation SHOULD support both modes.
- Prefix `ce-`, header values percent-encoded by HTTP's rule. **datacontenttype
  is not special**: it maps to `ce-datacontenttype` like any other attribute, and
  `Content-Type` is left to mean that a message is structured (D-NATS-1).
- **Mode detection inverts HTTP's default.** A CloudEvents content type means
  structured and anything else means binary, including no content type at all.
  No batch mode.
- **JSON only.** Implementations "MUST support the JSON event format" and the
  binding names no other, so the codec chooses the library and not the format.
- **No subject.** The spec defines no mapping from an event to a subject, so the
  binding derives none and takes none.
- **`not_a_cloudevent` is not reachable from the payload entry points**
  (`SWR-NATS-0004`): with no headers to consult, an unrelated JSON document
  cannot be told from a corrupt event. `from_message` can answer it, because a
  binary-mode message carries `ce-specversion`.

### 5.5 Typed extensions and payloads

- One described struct per documented extension, e.g.
  `tracing{ std::string traceparent; std::optional<std::string> tracestate; }`.
- `event::get<Ext>() -> result<Ext>` and `event::set(const Ext&)` map struct fields
  to and from extension attributes using the describe seam, with type conversion
  from the string form produced by lossy decoders.
- `event_of<T>`: thin typed view over `event` whose `data()` returns `result<T>`.

## 6. Testing

- Framework: boost-ext/ut, header mode, one executable per component, registered
  with CTest. Parameterise with `| std::vector{...}` for data and
  `| std::tuple<...>{}` for codec types.
- `static_assert` tests for every `constexpr` validator.
- Conformance fixtures in `test/fixtures/`: every example from the core, JSON and
  HTTP spec documents, stored verbatim, with expected decoded form.
- Interop fixtures: golden JSON produced by sdk-go and sdk-java for the same events;
  our output must decode in theirs and vice versa. Commit the goldens, not the SDKs.
- Negative tests for every `errc`.
- Round-trip property: `decode(encode(e)) == e` for generated events, all modes.
- Fuzz targets (libFuzzer) for `timestamp::parse`, `json_format::decode`,
  `http::from_message`, base64 decode. Must not crash, leak or time out; seed corpus
  from fixtures. Not part of the default build.
- Sanitizers: ASan+UBSan job on the full suite.
- Coverage target: 90 percent lines for `core`, `format`, `binding`.

## 7. Milestones

Each milestone ends with all tests green on every available preset.

**M0 Scaffold.** CMake 3.25+, presets (`gcc-cxx20`, `gcc-cxx23`, `clang-cxx20`,
`clang-cxx23`, `msvc-cxx20`, `reflect-cxx26`, `asan`, `fuzz`, and, added as the
configurations they guard arrived, `polyfill-cxx23`, `no-exceptions`,
`no-default-codec`, `coverage`), FetchContent pins by
commit hash for CTRE, nlohmann/json, ut, with `find_package` preferred when present.
`.clang-format`, `.clang-tidy`, GitHub Actions matrix, `detail/config.hpp`.
*Accept:* empty ut test builds and runs on all presets; install + `find_package(cloudevents)`
works from a consumer project in `test/consumer/`.

**M1 Core.** Build §5.1. Add `lint()`, string content rules from
core spec §3 (reject disallowed control characters on produce), full error coverage.
*Accept:* §5.1 contract tests pass under C++20 and C++23; zero warnings.

**M2 Describe seam.** §5.2, both backends, parity suite.
*Accept:* parity suite identical on macro and reflection backends; if the reflection
toolchain is unavailable locally, the backend is written, CI job defined, and the
gap is reported rather than hidden.

**M3 JSON.** §5.3 including base64, both codecs, conformance fixtures.
*Accept:* all JSON-format spec examples round-trip; suite passes for
`nlohmann_codec` and `mini_codec`; building with `CE_DEFAULT_CODEC=OFF` pulls no nlohmann.

**M4 HTTP binding.** §5.4.
*Accept:* all HTTP-binding spec examples pass in three modes; percent-encoding tests
include multi-byte UTF-8 and malformed sequences.

**M5 Typed layer.** §5.5 and the five documented extensions.
*Accept:* tracing extension survives JSON and HTTP-binary round-trips with types restored.

**M6 Release hardening.** Interop goldens, fuzz targets with 10-minute clean runs,
sanitizer job, coverage report, `examples/` (produce, consume, custom codec, custom
described payload), README, API reference via Doxygen, `cloudevents.cppm` wrapper.
*Accept:* tag `v0.1.0` candidate; checklist in `docs/RELEASE.md` complete.

> **Withdrawn in v0.4.0: the Doxygen API reference.** It was built as a CI artifact and
> published nowhere, so no reader ever reached it, and section 10 now reduces header
> comments to spec markers, which leaves it nothing to render. `docs/GUIDE.md` is the
> user-facing documentation instead. M6 shipped as written in v0.1.0; this note records
> the later removal rather than rewriting what was delivered.

## 8. Toolchain floor

GCC 13, Clang 16, MSVC 19.36 (VS 2022 17.6), AppleClang 16. Reflection job: newest
GCC with reflection enabled, or the Bloomberg clang-p2996 fork in a container;
verify which is available and record it. The floor may be raised only if CTRE or ut
require it, with a note in `docs/DECISIONS.md`.

**The real constraint is the standard library, not the compiler.** The SDK requires
`std::format`, so it requires a library that defines `__cpp_lib_format`: libstdc++
13 or newer, libc++ 17 or newer, or the MSVC STL from 19.29. Measured: GCC 12 has
no `<format>` at all; libc++ 16 has the header without `std::format` in it; and
libc++ 17 and 18 have a working `std::format` without defining
`__cpp_lib_format`, so `detail/config.hpp` carves them out by version (D-CI-1).
Compiler versions are therefore a shorthand; `detail/config.hpp` fails the build
with a named `#error` when the library is too old, so the diagnosis never depends
on reading this table.

## 9. Open decisions (owner: Filip)

Use the default, record it, move on. Do not relitigate inside a task.

| ID | Question | Default |
|---|---|---|
| D1 | `event` as public aggregate vs builder with private state | ~~Aggregate; `validate()` is the gate~~ **Reversed by CR-0001 / ADR-0008**: every context attribute is a type that cannot hold a forbidden value, `event` is constructed from validated attributes or through `builder`, and `validate()` is removed |
| D2 | License | Apache-2.0, matching the other CloudEvents SDKs |
| D3 | Repo and namespace name | `cloudevents-cpp`, `ce` |
| D4 | Support `-fno-exceptions` builds | Yes; verify ut and nlohmann configs permit it, else tests only need exceptions |
| D5 | Support specversion `0.3` on receive | No |
| D6 | Floating-point extension values (not in spec type system) | Reject on decode with `type_mismatch` |
| D7 | Package managers | vcpkg port and Conan recipe deferred past v0.1 |

## 10. Definition of done (every work item)

- Tests written first, cite spec sections, pass on all available presets
- No new warnings, clang-tidy clean, formatted
- No `#if` outside `detail/config.hpp` and describe backends
- Public entities in `include/` carry a `// spec: SWR-AREA-NNNN` marker and nothing
  else; a `// TODO(#NN):` naming an issue is the only other comment permitted there.
  Contract prose belongs in `docs/GUIDE.md`, rationale in `docs/DECISIONS.md`, a trap in
  a named test, and the story of a change in its commit message.
- `docs/DECISIONS.md` updated for any judgement call
- Commit message states which SPEC section it satisfies
