# cloudevents-cpp

A C++20 implementation of [CloudEvents v1.0.2](https://github.com/cloudevents/spec):
the core event model, the JSON event format and the HTTP protocol binding.

Header-only, no exceptions, and no HTTP or JSON library of its own. You supply
the JSON codec; the SDK never picks one for you.

```cpp
#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>

using codec = ce::codec::nlohmann_codec;

ce::event order{
    .id = "A234-1234-1234",
    .source = ce::uri_ref{"https://example.test/orders"},
    .type = "com.example.order.placed",
};
order.datacontenttype = "application/json";
order.data = ce::json_text{.raw = R"({"total":42})"};

auto request = ce::http::to_message<codec>(order, ce::content_mode::binary_mode);
```

## What you get

- **Three protocol bindings.** HTTP, Kafka and NATS, in both directions, over a
  transport-neutral `ce::message`. The SDK performs no network I/O and names no
  type from any client library: moving a message onto the wire is yours.
- **The three content modes.** Binary, structured and batched, where the binding
  defines them. Kafka has no batch mode and NATS is structured-only, and the SDK
  refuses what the transport does not have rather than inventing it.
- **Typed extensions.** The five documented CloudEvents extensions as structs.
  `event.get<ce::ext::tracing>()` returns a struct, and the declared field type
  is restored even when the wire form threw it away - which HTTP binary mode
  always does.
- **Typed payloads.** Describe your own struct once and carry it as the payload:
  `set_data<order>(event, value)` and `data_as<order>(event)`.
- **Your JSON library.** `ce::json::json_codec` is a concept over a DOM. An
  nlohmann codec ships in the box; anything else is about eighty lines.

## Requirements

C++20, and a standard library with `std::format`:

| standard library | minimum |
|---|---|
| libstdc++ | 13 |
| libc++ | 17 |
| MSVC STL | 19.29 |

The compiler version is a shorthand for the library version, and the library is
the real constraint. `detail/config.hpp` stops the build with a named `#error`
when the library is too old, so the diagnosis never depends on reading a table.

libc++ 17 and 18 implement `std::format` without defining `__cpp_lib_format`,
so the SDK carves them out by version. See `D-CI-1` in `docs/DECISIONS.md`.

Tested on GCC 13, Clang 16 with libstdc++ 13, Clang 17 with libc++ 17,
AppleClang, and MSVC 19.

## Installing

```bash
cmake -S . -B build
cmake --build build --target install
```

Then, from your project:

```cmake
find_package(cloudevents REQUIRED)
target_link_libraries(app PRIVATE ce::core ce::format_json ce::binding_http)
```

Four targets, and the dependency direction only goes downward:

| target | what it is | depends on |
|---|---|---|
| `ce::core` | the event model, validation, timestamps | CTRE, nothing else |
| `ce::format_json` | the JSON event format, over any codec | `ce::core` |
| `ce::binding_http` | the HTTP protocol binding | `ce::core` |
| `ce::binding_kafka` | the Kafka protocol binding | `ce::core` |
| `ce::binding_nats` | the NATS protocol binding | `ce::core` |
| `ce::codec_nlohmann` | the nlohmann codec, the default | `ce::core`, nlohmann |
| `ce::codec_rapidjson` | the RapidJSON codec, opt in with `-DCE_CODECS=` | `ce::core`, RapidJSON |
| `ce::codec_boost_json` | the Boost.JSON codec, opt in; needs exceptions | `ce::core`, Boost.JSON |

`ce::core` depends on no third-party library except CTRE, and a test in
`test/consumer/` is built against the installed package to keep it that way.

## Using your own JSON library

The format layer names only `Codec::` statics, so a codec is a struct of static
functions over your DOM. `examples/custom_codec.cpp` is a complete one.

```cpp
static_assert(ce::json::json_codec<my_codec>);
using format = ce::json_format<my_codec>;
```

The concept is what reports a missing operation, naming it, rather than failing
inside a template.

### Which codec

Three ship in the box. `bench/` measures them over CloudEvents-sized workloads;
these are its findings, not a recommendation to take on trust.

| codec | choose it when |
|---|---|
| `nlohmann` | you already depend on it, or you want the default and no decision |
| `rapidjson` | throughput matters: fastest on event-sized documents, smallest binary, shortest compile |
| `boost_json` | documents run large (it wins at 64 KiB), or you already link Boost |

`boost_json` needs exceptions; the build refuses it under `-fno-exceptions`
rather than failing at link. Glaze was measured too and is not shipped: it needs
C++23 and this SDK's floor is C++20.

## Errors

Nothing throws. Every fallible call returns `ce::result<T>`, which is
`std::expected` where the library has it and a polyfill of the same subset
where it does not.

```cpp
auto decoded = ce::http::from_message<codec>(request);
if (!decoded) {
  // code, a human-readable detail, and which attribute it was
  log(decoded.error().code, decoded.error().detail, decoded.error().where);
}
```

`errc::not_a_cloudevent` is deliberately distinct from a malformed event: a
receiver usually passes the first along unchanged rather than rejecting it.

## Examples

```bash
cmake -S . -B build -DCE_BUILD_EXAMPLES=ON
cmake --build build
```

- `examples/produce.cpp` - build an event, send it in all three modes
- `examples/consume.cpp` - receive without knowing which mode arrived
- `examples/custom_codec.cpp` - the SDK with a JSON library it has never seen
- `examples/described_payload.cpp` - carry your own struct as the payload

Each is also a test, because an example that compiles but does not work is
exactly what a reader hits first.

## Optional module interface

`cloudevents.cppm` re-exports the headers as a named module. It is off by
default, because module support across the supported toolchains is uneven and a
module target that switched itself on would break the floor configuration.

```bash
cmake -S . -B build -DCE_BUILD_MODULE=ON   # needs CMake 3.28+
```

See `D-MODULE-1` in `docs/DECISIONS.md` for what works and what does not.

## Interoperability

There is no reference C++ CloudEvents SDK to agree with, so agreement with the
Go and Java SDKs is the external correctness measure.

`test/fixtures/interop/` holds golden documents produced by each, and the
documents this SDK produced for them to read. `interop/run.sh` regenerates them
in Docker and fails if either SDK rejects anything this one wrote. The
generators are committed alongside their output.

One measured difference worth knowing: given the same `text/plain` payload, Go
and this SDK write a JSON string under `data`, while Java writes `data_base64`.
Both are permitted. A consumer that assumes either will break against the other.

## Building and testing

```bash
cmake --preset gcc-cxx20 && cmake --build --preset gcc-cxx20 && ctest --preset gcc-cxx20
```

Presets cover the toolchain floor, C++20 and C++23, the forced `result<T>`
polyfill, exceptions disabled, no default codec, C++26 static reflection,
sanitizers and fuzzing. CI runs all of them.

## Specification and traceability

`docs/SPEC.md` is the work specification. `spec/requirements/` holds the
requirements it was decomposed into, and every test cites the requirement it
verifies with a `// spec: SWR-AREA-NNNN` marker. Two gates run in CI and refuse
a stale link in either direction.

`docs/DECISIONS.md` records the judgement calls, with the measurement behind
each.

## Licence

Apache-2.0, matching the other CloudEvents SDKs.
