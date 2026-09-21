# Changelog

Notable changes per release. Dates are the tag date.

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
