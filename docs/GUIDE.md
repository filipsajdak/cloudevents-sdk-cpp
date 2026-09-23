# cloudevents-cpp user guide

This guide covers every feature of the SDK, one section per task.
Every C++ block below is compiled and run by the `example_guide` test, so the code is known to build against the current headers.

Unless a block says otherwise, it assumes these two lines:

```cpp
using codec = ce::codec::nlohmann_codec;
using namespace ce::literals;
```

## Contents

1. [Getting started](#1-getting-started)
2. [The event](#2-the-event)
3. [Errors](#3-errors)
4. [The JSON event format](#4-the-json-event-format)
5. [JSON codecs](#5-json-codecs)
6. [Protocol bindings](#6-protocol-bindings)
7. [Typed extensions](#7-typed-extensions)
8. [Typed payloads](#8-typed-payloads)
9. [Interoperability](#9-interoperability)
10. [How you can still get it wrong](#10-how-you-can-still-get-it-wrong)

## 1. Getting started

### Install

```bash
cmake -S . -B build
cmake --build build --target install
```

```cmake
find_package(cloudevents REQUIRED)
target_link_libraries(app PRIVATE ce::core ce::format_json ce::codec_nlohmann ce::binding_http)
```

Link only what you use.
Every target is header-only.

| target | contents | depends on |
|---|---|---|
| `ce::core` | the event, attribute types, timestamps, errors | CTRE |
| `ce::format_json` | the JSON event format, over any codec | `ce::core` |
| `ce::binding_http` | the HTTP binding | `ce::core` |
| `ce::binding_kafka` | the Kafka binding | `ce::core` |
| `ce::binding_nats` | the NATS binding | `ce::core` |
| `ce::codec_nlohmann` | the nlohmann codec, built by default | nlohmann/json |
| `ce::codec_rapidjson` | the RapidJSON codec, opt in with `-DCE_CODECS=` | RapidJSON |
| `ce::codec_boost_json` | the Boost.JSON codec, opt in; needs exceptions | Boost.JSON |

The SDK needs C++20 and a standard library with `std::format`: libstdc++ 13, libc++ 17 or MSVC STL 19.29.

### A first event

```cpp
#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>

auto order_request() -> ce::result<ce::message> {
  const ce::event order{
      "A234-1234-1234"_id,
      "https://example.test/orders"_source,
      "com.example.order.placed"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .data = ce::json_text{.raw = R"({"total":42})"},
      },
  };
  return ce::http::to_message<codec>(order, ce::content_mode::binary_mode);
}
```

`to_message` returns a `ce::message`: header fields and a body.
The SDK performs no network I/O, so putting that message on the wire is up to your HTTP client.

## 2. The event

### Attributes

Each attribute has its own type, and that type refuses every value CloudEvents forbids.
An event built from these types is valid, so there is no validation step to run before sending.

| attribute | type | literal | rule |
|---|---|---|---|
| `id` | `ce::id` | `"..."_id` | non-empty UTF-8 |
| `source` | `ce::source` | `"..."_source` | non-empty UTF-8 |
| `type` | `ce::type` | `"..."_type` | non-empty UTF-8 |
| `specversion` | `ce::spec_version` | none | always `1.0` |
| `datacontenttype` | `ce::datacontenttype` | `"..."_mediatype` | an RFC 2046 media type |
| `dataschema` | `ce::dataschema` | `"..."_dataschema` | non-empty UTF-8 |
| `subject` | `ce::subject` | `"..."_subject` | non-empty UTF-8 |
| `time` | `ce::timestamp` | none | RFC 3339 |
| an extension name | `ce::extension_name` | `"..."_ext` | `[a-z0-9]+`, not a context attribute name |

`source` is not checked against RFC 3986.
Real producers send values like `/sensors/7`, and refusing them helps no one.

### Constants are checked at compile time

A literal is checked when your program compiles, so a broken constant never reaches a running program:

```cpp nocompile
auto nothing = ""_id;          // does not compile: id must be non-empty
auto shadow = "subject"_ext;   // does not compile: subject is a context attribute
```

The compiler's error names `this_literal_is_not_a_valid_cloudevents_attribute`.

### Text from outside is checked at run time

Text read at run time goes through the type's `make()`, which returns a `ce::result`:

```cpp body
const std::string from_the_wire = "order-7";
auto subject = ce::subject::make(from_the_wire);
if (!subject) {
  std::printf("%s: %s\n", subject.error().where.c_str(), subject.error().detail.c_str());
}
```

`make()` takes a `std::string` or a `std::string_view`.
Pass one of those rather than a string literal, because a literal converts to both and the call is ambiguous.
For a constant, use the literal form instead.

### Building an event

The constructor takes the three required attributes, and optionally a `ce::event::options` holding everything else:

```cpp body
const ce::event minimal{"1"_id, "/sensors/7"_source, "com.example.reading"_type};

const ce::event full{
    "2"_id,
    "/sensors/7"_source,
    "com.example.reading"_type,
    {
        .datacontenttype = "text/plain"_mediatype,
        .subject = "temperature"_subject,
        .extensions = {{"region"_ext, std::string{"eu-north"}}},
        .data = std::string{"21.5"},
    },
};
std::printf("%s and %s\n", minimal.id().str().c_str(), full.id().str().c_str());
```

The members of `options` must appear in this order: `datacontenttype`, `dataschema`, `subject`, `time`, `extensions`, `data`.
Any of them can be left out.

Both constructors are `explicit`, so a bare `return {...};` does not compile.
Write `return ce::event{...};`.

### Reading attributes

Every attribute has a `const` accessor.
The attribute types expose `view()` (a `std::string_view`) and `str()` (a `std::string`):

```cpp body
const ce::event reading{"3"_id, "/sensors/7"_source, "com.example.reading"_type,
                        {.subject = "humidity"_subject}};
std::printf("%s from %s\n", reading.type().str().c_str(), reading.source().str().c_str());
if (const auto& about = reading.subject(); about) {
  std::printf("about %s\n", about->str().c_str());
}
std::printf("specversion %s\n", std::string{ce::event::specversion().view()}.c_str());
```

Optional attributes come back as `const std::optional<T>&`.

### The payload

`data()` returns a `ce::data_t`, a variant of four alternatives:

| alternative | meaning |
|---|---|
| `std::monostate` | no payload |
| `std::string` | text |
| `ce::binary` | bytes (`std::vector<std::byte>`) |
| `ce::json_text` | JSON the SDK carries without parsing |

`set_data` replaces the payload and its media type together, so the event never says its bytes are something they are not:

```cpp body
ce::event report{"4"_id, "/reports"_source, "com.example.report"_type};
report.set_data(ce::json_text{.raw = R"({"rows":3})"}, "application/json"_mediatype);
report.set_data(ce::to_bytes("\x89PNG"), "image/png"_mediatype);
report.set_data(std::monostate{}, std::nullopt);
```

`ce::to_bytes` and `ce::to_text` convert between text and `ce::binary`.

### Extension attributes

An extension value is a `ce::attribute_value`: one of `bool`, `std::int32_t`, `std::string`, `ce::binary`, `ce::uri`, `ce::uri_ref` or `ce::timestamp`.
The CloudEvents type system has no floating-point type, so neither does the variant.

```cpp body
ce::event job{"5"_id, "/scheduler"_source, "com.example.job.started"_type};
job.set_extension("priority"_ext, std::int32_t{3});
job.set_extension("queue"_ext, std::string{"batch"});

if (const ce::attribute_value* priority = job.extension("priority")) {
  std::printf("priority %d\n", std::get<std::int32_t>(*priority));
}
if (job.remove_extension("queue")) {
  std::printf("%zu extension left\n", job.extensions().size());
}
```

A string value must be spelled `std::string{...}`.
A bare string literal could become a `std::string`, a `ce::uri` or a `ce::uri_ref`, so the call does not compile.

A name known only at run time goes through `ce::extension_name::make`:

```cpp body
ce::event job{"6"_id, "/scheduler"_source, "com.example.job.started"_type};
const std::string configured = "tenant";
if (auto name = ce::extension_name::make(configured); name) {
  job.set_extension(std::move(*name), std::string{"acme"});
}
```

### Timestamps

`ce::parse_timestamp` reads RFC 3339, and `ce::to_string` writes it back exactly as it was written:

```cpp body
if (auto when = ce::parse_timestamp("2026-09-20T12:34:56.120+02:00"); when) {
  std::printf("%s\n", ce::to_string(*when).c_str());
}

const ce::timestamp now{
    .utc = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()),
    .fractional_digits = 3,
};
std::printf("%s\n", ce::to_string(now).c_str());
```

A `ce::timestamp` holds the instant in UTC plus enough of its spelling to reproduce it: the offset, whether it was `Z` or `+00:00`, and how many fractional digits it had.
`fractional_digits` accepts 0 to 9, and a literal outside that range does not compile.

Parsing is lenient in two ways.
A lowercase `t` or `z` is accepted, and a seconds field of `60` (a leap second) folds onto the following second.
Neither of these round-trips.
An instant outside roughly 1678 to 2262 is refused with `out_of_range`, because a nanosecond clock cannot hold it.

### Building an event one field at a time

A decoder learns attributes one at a time, so it cannot name all three required attributes in one expression.
`ce::event::builder` holds them as optionals, and `build()` reports the first one that is missing:

```cpp body
ce::event::builder pending{};
if (auto parsed = ce::id::make(std::string_view{"from-a-parser"}); parsed) {
  pending.id = std::move(*parsed);
}
auto built = std::move(pending).build();
if (!built) {
  std::printf("missing %s\n", built.error().where.c_str());
}
```

A missing attribute is the only way `build()` fails.
The attribute types have already refused every other mistake.

### Lint

`lint()` reports what the specification says SHOULD NOT happen but still permits.
It never rejects an event:

```cpp body
ce::event noisy{"7"_id, "/s"_source, "com.example.noisy"_type};
noisy.set_extension("averyveryverylongextensionname"_ext, true);
for (const ce::lint_warning& warning : noisy.lint()) {
  std::printf("%s: %s\n", warning.attribute.c_str(), warning.message.c_str());
}
```

Today it reports one thing: an extension name longer than 20 characters.

## 3. Errors

Nothing in the SDK throws.
Every fallible call returns `ce::result<T>`, which is `std::expected<T, ce::error>` where the standard library has it, and a polyfill of the same subset where it does not.

A `ce::error` has three members:

- `code`, a `ce::errc`;
- `detail`, a sentence for a human;
- `where`, the attribute, header or JSON member concerned.

```cpp body
auto decoded =
    ce::json_format<codec>::decode(R"({"specversion":"0.3","id":"1","source":"/s","type":"t"})");
if (!decoded && decoded.error().code == ce::errc::unsupported_spec_version) {
  std::printf("%s\n", decoded.error().detail.c_str());
}
```

To pass a failure up to your own caller, return it as a `ce::failure`:

```cpp
auto order_id(std::string_view text) -> ce::result<std::string> {
  auto decoded = ce::json_format<codec>::decode(text);
  if (!decoded) {
    return ce::failure{decoded.error()};
  }
  return decoded->id().str();
}
```

`ce::to_string_view(code)` names an `errc` for a log line.

### Every error code

| `errc` | what produces it |
|---|---|
| `missing_required_attribute` | an empty `id`, `source` or `type`; a required attribute absent from a document or a message; `build()` with a required attribute unset; `data_as` on an event with no payload; a typed extension missing a required field |
| `invalid_attribute_name` | an extension name that is not `[a-z0-9]+` |
| `reserved_attribute_name` | an extension name that is also a context attribute name, such as `subject` |
| `invalid_attribute_value` | an empty `subject`, `dataschema` or `datacontenttype`; a context attribute that is not a JSON string; a fractional digit count above nine; `sampled_rate::validate()` on a rate below one |
| `unsupported_spec_version` | a `specversion` other than `1.0` |
| `invalid_timestamp` | text that is not an RFC 3339 date-time, or names a date or time that does not exist |
| `invalid_content_type` | a `datacontenttype` that is not a media type |
| `parse_error` | text the codec cannot parse; a document that is not a JSON object; a batch that is not a JSON array; a `json_text` payload that is not JSON, found when encoding |
| `type_mismatch` | a floating-point, array or object extension value; `data_as` on a payload that is not JSON; a typed extension or payload field whose value has the wrong type |
| `out_of_range` | an integer extension outside 32 bits; a timestamp outside roughly 1678 to 2262; an integer too large for a typed payload field |
| `data_conflict` | a JSON document carrying both `data` and `data_base64` |
| `invalid_base64` | `data_base64` that is not RFC 4648 section 4 base64, including over-padding and the URL-safe alphabet |
| `invalid_utf8` | an attribute value, or a header value on either side of a binding, that is not well-formed UTF-8 |
| `not_a_cloudevent` | an HTTP, Kafka or NATS message with neither a `specversion` field nor a CloudEvents content type; a batch reader given a message that is not a batch |
| `invalid_argument` | a batch passed to the single-event entry point, or the reverse; a batch on Kafka or NATS, which define none; two header fields carrying one attribute; `ce-datacontenttype` on HTTP or Kafka, which carry it in their content-type field; a control character under `http::literal_values` |

`not_a_cloudevent` is kept apart from a malformed event on purpose.
A receiver usually passes such a message along unchanged, while it rejects a malformed one.

## 4. The JSON event format

`ce::json_format<Codec>` encodes and decodes the [JSON event format](https://github.com/cloudevents/spec/blob/v1.0.2/cloudevents/formats/json-format.md), one event or a batch:

```cpp body
const ce::event greeting{"8"_id, "/s"_source, "com.example.greeting"_type,
                         {.datacontenttype = "text/plain"_mediatype,
                          .data = std::string{"hello"}}};

auto text = ce::json_format<codec>::encode(greeting);
if (text) {
  auto back = ce::json_format<codec>::decode(*text);
  std::printf("round trip %s\n", back && *back == greeting ? "equal" : "different");
}

const std::vector<ce::event> events{greeting, greeting};
if (auto batch = ce::json_format<codec>::encode_batch(events); batch) {
  auto decoded = ce::json_format<codec>::decode_batch(*batch);
  std::printf("%zu events\n", decoded ? decoded->size() : 0U);
}
```

`to_value` and `from_value` do the same against a codec's DOM, for a caller that already holds one.
The media types are `json_format<Codec>::content_type` and `batch_content_type`.

### How the payload is written

| `data_t` | JSON |
|---|---|
| `std::monostate` | no `data` member |
| `std::string` | `"data": "<the text>"` |
| `ce::binary` | `"data_base64": "<base64>"` |
| `ce::json_text` | `"data": <the JSON itself>`, parsed and spliced in |

### How the payload is read

- `data_base64` becomes `ce::binary`.
- `data` holding a JSON string, under a `datacontenttype` that is not JSON, becomes `std::string`.
- Any other `data` becomes `ce::json_text`.
  That includes a string when `datacontenttype` is absent, because the format says an absent content type means JSON.

### What decoding loses

JSON has no room for the CloudEvents attribute type, so an extension's declared type is inferred on decode:

| JSON value | decoded as |
|---|---|
| `true` / `false` | `bool` |
| an integer | `std::int32_t`, or `out_of_range` outside 32 bits |
| a string | `std::string` |
| `null` | the attribute is unset |
| a fraction, an array or an object | refused with `type_mismatch` |

So a `ce::uri`, `ce::uri_ref`, `ce::timestamp` or `ce::binary` extension comes back as `std::string`.
[Typed extensions](#7-typed-extensions) restore the declared type.

## 5. JSON codecs

The format layer never names a JSON library.
It talks to a codec: a struct of static functions over some JSON DOM, checked by the `ce::json::json_codec` concept.

| codec | header | choose it when |
|---|---|---|
| `ce::codec::nlohmann_codec` | `<cloudevents/codec/nlohmann.hpp>` | you already use nlohmann, or want the default |
| `ce::codec::rapidjson_codec` | `<cloudevents/codec/rapidjson.hpp>` | throughput matters: fastest on event-sized documents, smallest binary |
| `ce::codec::boost_json_codec` | `<cloudevents/codec/boost_json.hpp>` | documents are large, or you already link Boost; needs exceptions |

Build with `-DCE_CODECS="nlohmann;rapidjson"` to choose which codec targets exist.
`bench/` holds the measurements behind the table.

### Writing your own

`examples/custom_codec.cpp` is a complete codec over a DOM the SDK has never seen.
Check yours with the concept, which names any missing operation:

```cpp nocompile
static_assert(ce::json::json_codec<my_codec>);
using format = ce::json_format<my_codec>;
```

A codec supplies `parse`, `dump`, the `make_*` constructors, `set`, `push`, `kind_of`, `find`, `size_of`, the `as_*` readers, `for_each_member` and `for_each_element`.
Three rules keep codecs in agreement:

- `kind_of` reports `kind::integer` for a number written with no fraction and no exponent, whatever its size.
- `as_int` returns `out_of_range` for an integer it cannot represent; it never returns a different value.
- `find` returns `nullptr` for an absent member, because absent and `null` mean different things.

## 6. Protocol bindings

A binding maps an event to a `ce::message` and back:

```cpp nocompile
struct message {
  ce::raw_headers header_fields;
  ce::binary body;
};
```

`raw_headers` keeps fields in order and keeps duplicates, because that is what a transport delivers.
When a binding reads a message, it refuses two fields that carry the same attribute with `invalid_argument`, rather than guessing which one was meant.
When you build a message, use `set` rather than `add` unless you mean to repeat a field.

### The bindings side by side

| | HTTP | Kafka | NATS |
|---|---|---|---|
| namespace | `ce::http` | `ce::kafka` | `ce::nats` |
| attribute prefix | `ce-` | `ce_` | `ce-` |
| `datacontenttype` travels as | `Content-Type` | `content-type` | `ce-datacontenttype` |
| field names match | case-insensitively | byte for byte | case-insensitively |
| values | percent-encoded | UTF-8, not escaped | percent-encoded |
| binary mode | yes | yes | yes, NATS 2.2 or later |
| structured mode | yes | yes | yes |
| batched mode | yes | no | no |

Every binding offers `to_message<Codec>(event, mode)`, `from_message<Codec>(message)` and `detect_content_mode(message)`.
Asking for a mode a binding does not define returns `invalid_argument`; the SDK never invents one.

### HTTP

```cpp body
const ce::message request{
    .header_fields = {
        {"ce-specversion", "1.0"},
        {"ce-id", "A1"},
        {"ce-source", "/orders"},
        {"ce-type", "com.example.order.placed"},
        {"Content-Type", "application/json"},
    },
    .body = ce::to_bytes(R"({"total":42})"),
};

auto received = ce::http::from_message<codec>(request);
if (!received && received.error().code == ce::errc::not_a_cloudevent) {
  std::printf("not a CloudEvent: pass it along\n");
} else if (received) {
  std::printf("received %s\n", received->id().str().c_str());
}
```

`from_message` detects the mode itself.
A batch has its own pair, `to_batch_message<Codec>(events)` and `from_batch_message<Codec>(message)`, and `detect_content_mode` tells you which to call.

Header values are percent-encoded as the binding specification requires.
The Go and Java SDKs do not do this; see [Interoperability](#9-interoperability) for the opt-out.

### Kafka

A Kafka record adds a key, which decides the partition.
`to_record` takes a key mapper, and `partitionkey_mapper` uses the `partitionkey` extension:

```cpp body
ce::event placed{"9"_id, "/orders"_source, "com.example.order.placed"_type};
placed.set_extension("partitionkey"_ext, std::string{"customer-42"});

auto record =
    ce::kafka::to_record<codec, ce::kafka::partitionkey_mapper>(placed, ce::content_mode::binary_mode);
if (record && record->key) {
  std::printf("key %s, %zu headers\n", record->key->c_str(), record->value.header_fields.size());
}
```

The default mapper, `no_key_mapper`, sets no key, so the broker spreads records round-robin.
Write your own as a struct with `static auto key_of(const ce::event&) -> std::optional<std::string>`.
Record header keys are byte strings, so `CE_ID` is not `ce_id`.

### NATS

NATS has two pairs of functions.
`to_payload` and `from_payload` exchange the JSON event as text, which every NATS server can carry.
`to_message` and `from_message` add binary mode, which needs headers and so NATS 2.2 or later:

```cpp body
const ce::event reading{"10"_id, "/sensors/7"_source, "com.example.reading"_type};

auto payload = ce::nats::to_payload<codec>(reading);
auto headed = ce::nats::to_message<codec>(reading, ce::content_mode::binary_mode);
std::printf("%s / %zu headers\n", payload ? payload->c_str() : "failed",
            headed ? headed->header_fields.size() : 0U);
```

Choosing a NATS subject is up to you, because the binding defines no mapping from an event to one.
`from_payload` cannot answer `not_a_cloudevent`: a bare payload has no header to consult, so an unrelated JSON document looks exactly like a malformed event.

## 7. Typed extensions

The five documented CloudEvents extensions ship as structs in `<cloudevents/extensions.hpp>`:

| struct | attributes |
|---|---|
| `ce::ext::tracing` | `traceparent`, optional `tracestate` |
| `ce::ext::partitioning` | `partitionkey` |
| `ce::ext::sampled_rate` | `sampledrate` |
| `ce::ext::sequence` | `sequence` |
| `ce::ext::dataref` | `dataref` |

`set` writes a struct into the extension attributes, and `get` reads it back:

```cpp body
ce::event traced{"11"_id, "/s"_source, "com.example.traced"_type};
if (auto stored = traced.set(ce::ext::tracing{
        .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"});
    !stored) {
  std::printf("%s\n", stored.error().detail.c_str());
}

if (auto tracing = traced.get<ce::ext::tracing>(); tracing) {
  std::printf("%s\n", tracing->traceparent.c_str());
}
```

`get` converts from text where the wire form lost the type, so a `sampledrate` that arrived as the string `"10"` reads back as `std::int32_t{10}`.
An absent optional field reads as `std::nullopt`, and an absent required field fails with `missing_required_attribute`.
`set` with an optional field that is `std::nullopt` removes that attribute.

`sampled_rate` must be above zero, which the attribute type system cannot express.
Call `validate()` on the struct to check it.

### Your own extension

Describe a struct with `CE_DESCRIBE`, in the struct's own namespace:

```cpp
namespace shop {

struct tenancy {
  std::string tenant;
  std::optional<std::int32_t> shard = {};
};

CE_DESCRIBE(tenancy, tenant, shard);

}  // namespace shop
```

A field may be `bool`, `std::int32_t`, `std::string`, `ce::uri`, `ce::uri_ref` or `ce::timestamp`, each optionally in `std::optional`.
Anything else is a compile error naming the struct.
Each field name becomes an attribute name, so it must match `[a-z0-9]+`; `set` refuses one that does not.
`CE_FIELD(member, "wirename")` gives a member a different attribute name.

## 8. Typed payloads

A described struct can be the payload.
`ce::set_data` writes it as JSON and sets `datacontenttype` to `application/json`, and `ce::data_as` reads it back:

```cpp
namespace shop {

struct line_item {
  std::string sku;
  std::int32_t quantity;
};

CE_DESCRIBE(line_item, sku, quantity);

inline void carry_a_line_item() {
  ce::event added{"12"_id, "/cart"_source, "com.example.cart.added"_type};
  ce::set_data<line_item, codec>(added, line_item{.sku = "SKU-1", .quantity = 2});

  if (auto item = ce::data_as<line_item, codec>(added); item) {
    std::printf("%s x%d\n", item->sku.c_str(), item->quantity);
  }
}

}  // namespace shop
```

A payload field may be `bool`, `std::int32_t`, `std::int64_t`, `double`, `std::string`, or a `std::optional`, `std::vector` or `std::map<std::string, ...>` of those.

Reading is lenient about absent members and strict about present ones.
A missing member keeps its default, so a struct can gain a field and still read older documents.
A member of the wrong type fails, and `where` names the member.

`ce::event_of<T, Codec>` puts the payload type into the event's type, so producer and consumer share it at compile time:

```cpp body
auto view = ce::event_of<shop::line_item, codec>::with_data(
    ce::event{"13"_id, "/cart"_source, "com.example.cart.added"_type},
    shop::line_item{.sku = "SKU-2", .quantity = 1});
if (auto item = view.data(); item) {
  std::printf("%s\n", item->sku.c_str());
}
const ce::event& underlying = view.underlying();
std::printf("%s\n", underlying.id().str().c_str());
```

`examples/described_payload.cpp` covers vectors, maps, optionals and the failure cases.

### C++26 reflection

Under C++26 with static reflection (GCC 16 with `-freflection`), an annotated struct needs no macro:

```cpp nocompile
struct [[=ce::reflect{}]] line_item {
  [[=ce::name("id")]] std::string sku;
  std::int32_t quantity;
  [[=ce::skip{}]] std::string cache;
};
```

Reflection never adopts a type on its own: without `ce::reflect`, a struct stays undescribed.
A struct with `CE_DESCRIBE` keeps its macro description when reflection is on.

## 9. Interoperability

`interop/run.sh` checks this SDK against the Go and Java SDKs in both directions.
Three differences are worth knowing.

**HTTP binary mode and percent-encoding.**
The binding specification requires header values containing a space, `"`, `%` or non-ASCII to be percent-encoded.
sdk-go v2.15.2 and sdk-java neither encode nor decode, so an event whose `subject` is `a b` reaches a Go application as `a%20b`.
Name the opt-in policy on both sides when you talk to them:

```cpp body
const ce::event spaced{"14"_id, "/s"_source, "com.example.t"_type, {.subject = "a b"_subject}};
auto request = ce::http::to_message<codec, ce::http::literal_values>(
    spaced, ce::content_mode::binary_mode);
if (request) {
  auto back = ce::http::from_message<codec, ce::http::literal_values>(*request);
  std::printf("%s\n", back ? back->subject()->str().c_str() : "failed");
}
```

`literal_values` still refuses a control character, because a CR or LF in a header value would let a sender start a new header.
Kafka is unaffected: no SDK escapes Kafka header values.

**Text payloads in JSON.**
Given the same `text/plain` payload, Go and this SDK write a JSON string under `data`, while Java writes `data_base64`.
Both are permitted, so a consumer must accept either.

**Extension types.**
Every SDK loses an extension's declared type in JSON and in binding headers.
Read typed extensions with `get<Ext>()` rather than comparing variant alternatives.

## 10. How you can still get it wrong

The types rule out an invalid event, but they cannot rule out these.

| mistake | what happens | what to do |
|---|---|---|
| A text payload with no `datacontenttype` | It is decoded as `json_text` holding `"hello"`, quotes included, because absent means JSON. | Always set `datacontenttype` for text. |
| Trusting a JSON payload received in binary mode | The body is carried as `json_text` unparsed; malformed JSON surfaces only when you parse it. | Treat `json_text` as untrusted until `data_as` or your own parser accepts it. |
| Comparing an extension after a round trip | A `ce::uri` extension comes back as `std::string`, so the events differ. | Read it with a typed extension. |
| Reading a payload as the wrong struct | Absent members default, so it succeeds with empty fields. | Switch on `type` before choosing the struct. |
| Holding the pointer `extension()` returned | It dangles after `set_extension`, `remove_extension`, `set`, or the event's destruction. | Copy the value if you need it longer. |
| Building headers with `add` | Two fields of one attribute are refused on receipt. | Use `raw_headers::set`. |
| HTTP binary mode to a Go or Java peer | Values with spaces or non-ASCII arrive percent-encoded. | Use `ce::http::literal_values` on both sides. |
| `sampled_rate{0}` | `set` stores it; only `validate()` refuses it. | Call `validate()` before `set`. |
| A leap second or a lowercase `t` | Accepted, but written back differently. | Compare instants, not strings. |
| `ce::id::make("abc")` | Does not compile: the literal converts to both `make` overloads. | Use `"abc"_id`, or pass a `std::string`. |

## Also available

- **A C++20 module.** Build with `-DCE_BUILD_MODULE=ON` (CMake 3.28+) and `import cloudevents;`.
  It is off by default, because module support varies across the supported toolchains.
  `D-MODULE-1` in [DECISIONS.md](DECISIONS.md) records what works.
- **base64.** `ce::base64_encode` and `ce::base64_decode`, in `<cloudevents/format/base64.hpp>`, follow RFC 4648 section 4 strictly.
