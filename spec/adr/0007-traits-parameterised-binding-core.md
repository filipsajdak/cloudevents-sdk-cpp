# ADR-0007: A binding is a traits type over a shared core

## Status

Accepted 2026-09-21.

## Context

`v0.1.0` shipped one protocol binding. The interop audit found that every other
major CloudEvents SDK ships several, and reading the Kafka, AMQP, MQTT, NATS and
WebSockets specifications produced the decisive fact: none of them needs a
transport client library. They are all value-to-value mappings, exactly as the
HTTP binding is.

They are also mostly the same mapping. Attributes become named fields under a
prefix, the payload becomes the body, and structured mode puts the whole event in
the body under a content type. Copying `to_message` and `from_message` per binding
would put the attribute set, the emission order, the error precedence and the
extension-name rule in as many places as there are transports.

What actually differs is small: the prefix, the field name that carries the
content type, whether field names compare case-insensitively, and how a value is
escaped.

## Decision

The shared rules live once, in `ce::binding`, as function templates over a
`binding_traits` type. A binding supplies the traits and keeps only what is its
own.

A traits type rather than function parameters. `attribute_prefix` and
`case_sensitive_names` are needed in constant expressions, and a `std::function`
would allocate and add an indirect call per field in a header-only SDK that must
build with `-fno-exceptions`.

The non-codec templates are parameterised on the traits alone, never on the codec,
so they instantiate once per binding rather than once per binding-codec pair.

Traits types live in a **named** `detail` namespace. An anonymous namespace in a
header gives every translation unit its own type, an ODR violation the linker does
not report.

`ce::binding` is public, not `detail`. A third-party binding author wants exactly
these pieces, and a binding written outside this repository is the case the design
is for.

## One flag, not two

`case_sensitive_names` decides prefix matching, attribute-name normalisation,
content-type lookup and whether writing a field replaces a differently-cased one.

An earlier shape gave the traits a `put` hook for the write side and the flag for
the read side. That permits a binding that lowers a name while reading and
preserves it while writing, which does not round-trip, and the two halves are far
enough apart in the code that a reviewer would not see the pair. Deriving both
from one member removes the inconsistent state instead of testing for it.

The cost is a binding that wants case-sensitive names with replace-nothing
semantics, or duplicate fields, which none of the six CloudEvents bindings does.
Such a binding calls `render_attribute` and the `headers` members directly.

## What stays with HTTP

`detect_content_mode`, `to_batch_message` and `from_batch_message` keep their
bodies in `binding/http.hpp`. Batch mode is HTTP's alone among the bindings this
SDK implements, and a core function with one caller is a worse home than the
caller.

## Consequences

The HTTP binding's observable behaviour is unchanged, which is the whole
acceptance criterion: no existing test file changes.

Half of each `if constexpr` in the core is unreachable from HTTP, so it would ship
unexercised. `test/binding_core_test.cpp` drives the core through a stand-in
traits type that takes the opposite branch everywhere. The stand-in is
deliberately not Kafka's: it makes the core's contract the thing under test, and
the Kafka binding will bring its own suite for its own rules.

Emission order becomes a contract (`SWR-BIND-0002`) rather than an accident of how
the code is arranged, because the interop and conformance fixtures compare whole
messages and a reordering fails a long way from the change that caused it.
