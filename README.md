# cloudevents-cpp

A header-only C++20 SDK for [CloudEvents v1.0.2](https://github.com/cloudevents/spec): the event model, the JSON event format, and the HTTP, Kafka and NATS bindings.

It throws no exceptions, performs no network I/O and brings no JSON library of its own.
You choose the codec.

```cpp
#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>

using codec = ce::codec::nlohmann_codec;
using namespace ce::literals;

const ce::event order{
    "A234-1234-1234"_id,
    "https://example.test/orders"_source,
    "com.example.order.placed"_type,
    {
        .datacontenttype = "application/json"_mediatype,
        .data = ce::json_text{.raw = R"({"total":42})"},
    },
};

auto request = ce::http::to_message<codec>(order, ce::content_mode::binary_mode);
```

Each attribute is a type that refuses what CloudEvents forbids, and a literal is checked at compile time: `""_id` does not build.
So an invalid event cannot be written down, and there is no validation step to forget.

## What you get

- **Three bindings**: HTTP, Kafka and NATS, in both directions, over a transport-neutral `ce::message`.
- **Every content mode the binding defines**: binary, structured and batched. The SDK refuses a mode the transport does not have.
- **Typed extensions**: the five documented extensions as structs, with the declared type restored after the wire loses it.
- **Typed payloads**: describe a struct once and carry it as the payload.
- **Your JSON library**: a codec is a concept over a DOM. nlohmann, RapidJSON and Boost.JSON codecs ship in the box.

## Requirements

C++20, and a standard library with `std::format`:

| standard library | minimum |
|---|---|
| libstdc++ | 13 |
| libc++ | 17 |
| MSVC STL | 19.29 |

Tested on GCC 13, Clang 16 with libstdc++ 13, Clang 17 with libc++ 17, AppleClang and MSVC 19.
`detail/config.hpp` stops the build with a named `#error` on an older library.

## Installing

```bash
cmake -S . -B build
cmake --build build --target install
```

```cmake
find_package(cloudevents REQUIRED)
target_link_libraries(app PRIVATE ce::core ce::format_json ce::codec_nlohmann ce::binding_http)
```

`ce::core` depends on nothing but CTRE.

## Documentation

- [docs/GUIDE.md](docs/GUIDE.md) - the user guide: every feature, with examples that are compiled and run in CI.
- [examples/](examples/) - four complete programs: produce, consume, a custom codec and a described payload.
- [docs/README.md](docs/README.md) - which of the other documents to read.

## Contributing and security

[CONTRIBUTING.md](CONTRIBUTING.md) covers the build, the spec-first workflow and the gates.
[SECURITY.md](SECURITY.md) says how to report a vulnerability.

## Licence

Apache-2.0, matching the other CloudEvents SDKs.
