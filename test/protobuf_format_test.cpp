#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "protobuf_support.hpp"

// The Protobuf event format: the entry points, the schema, the attribute types and the time.

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;
using namespace ce::test::pb;
using protobuf = ce::protobuf_format;

}  // namespace

// spec: SWR-PROTO-0001
const boost::ut::suite<"protobuf-format-entry-points"> entry_points = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "encode and decode are static, take an event and bytes, and return a result"_test = [] {
    const auto event = base_event();
    static_assert(std::is_same_v<decltype(protobuf::encode(event)), ce::result<ce::binary>>);
    static_assert(std::is_same_v<decltype(protobuf::decode(ce::binary{})), ce::result<ce::event>>);
    const auto written = protobuf::encode(event);
    expect(written.has_value());
    const auto read = protobuf::decode(*written);
    expect(read.has_value() && *read == event);
  };

  "the options are a designated-initializer aggregate"_test = [] {
    const ce::protobuf::decode_options tight{.max_encoded_size = 7, .max_attribute_count = 2};
    expect(tight.max_encoded_size == 7U && tight.max_attribute_count == 2U);
  };
};

// spec: SWR-PROTO-0002
const boost::ut::suite<"protobuf-format-media-type"> media_type = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the media type of an encoded event is application/cloudevents+protobuf"_test = [] {
    expect(protobuf::content_type == "application/cloudevents+protobuf"sv);
    expect(ce::protobuf::content_type == "application/cloudevents+protobuf"sv);
  };
};

// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-named-limits"> named_limits = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the default options carry the named limits ruled by the owner"_test = [] {
    constexpr ce::protobuf::decode_options defaults{};
    expect(defaults.max_encoded_size == ce::protobuf::default_max_encoded_size);
    expect(defaults.max_attribute_count == ce::protobuf::default_max_attribute_count);
    constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
    expect(ce::protobuf::default_max_encoded_size == std::size_t{16} * mebibyte);
    expect(ce::protobuf::default_max_attribute_count == 1024U);
    expect(ce::protobuf::fixed_nesting_depth == 4U);
  };
};

// spec: SWR-PROTO-0003
// spec: SWR-PROTO-0004
const boost::ut::suite<"protobuf-field-numbers-match-the-proto"> field_numbers = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the required attributes are the fields 1 to 4"_test = [] {
    expect(encoded_form(base_event()) == event_bytes());
  };

  "an optional attribute is a map entry in field 5, keyed by its name"_test = [] {
    const auto subject = base_event({.subject = "s"_subject});
    expect(encoded_form(subject) == event_bytes({string_entry("subject", "s")}));
    const auto read = decoded(event_bytes({string_entry("subject", "s")}));
    expect(read.has_value() && *read == subject);
  };

  "datacontenttype, dataschema, subject and time are map entries, not fields"_test = [] {
    const auto context = base_event({
        .datacontenttype = "text/plain"_mediatype,
        .dataschema = "https://example.com/s"_dataschema,
        .subject = "s"_subject,
        .time = utc_instant("1970-01-01T00:00:01Z"),
    });
    expect(encoded_form(context) ==
           event_bytes({string_entry("datacontenttype", "text/plain"),
                        entry("dataschema", text(ce_uri, "https://example.com/s")),
                        string_entry("subject", "s"),
                        entry("time", message(ce_timestamp, varint(seconds_field, 1)))}));
    const auto read = decoded(encoded_form(context));
    expect(read.has_value() && *read == context);
  };

  "the three data fields are 6, 7 and 8"_test = [] {
    expect(encoded_form(base_event({.data = ce::binary{std::byte{1}}})) ==
           event_bytes({text(binary_data_field, "\x01"sv)}));
    expect(encoded_form(base_event({.data = std::string{"hi"}})) ==
           event_bytes({text(text_data_field, "hi")}));
    const auto any = base_event({.datacontenttype = "application/protobuf"_mediatype,
                                 .dataschema = "type.example/T"_dataschema,
                                 .data = ce::binary{std::byte{1}}});
    expect(encoded_form(any) ==
           event_bytes({string_entry("datacontenttype", "application/protobuf"),
                        entry("dataschema", text(ce_uri, "type.example/T")),
                        message(proto_data_field, any_body("type.example/T", "\x01"sv))}));
  };

  "a field the schema does not know is skipped, whatever its wire type"_test = [] {
    ce::binary fixed32;
    wire::put_tag(fixed32, unknown_field, wire::wire_type::fixed32);
    fixed32.insert(fixed32.end(), 4, std::byte{0});
    ce::binary fixed64;
    wire::put_tag(fixed64, unknown_field, wire::wire_type::fixed64);
    fixed64.insert(fixed64.end(), 8, std::byte{0});
    const auto read = decoded(
        event_bytes({text(unknown_field, "ignored"), varint(unknown_field, 7), fixed32, fixed64}));
    expect(read.has_value() && *read == base_event());
  };

  "a known field in the wrong wire type is skipped as unknown"_test = [] {
    const auto read =
        decoded(event_bytes({varint(text_data_field, 7), varint(attributes_field, 1)}));
    expect(read.has_value() && *read == base_event());
  };
};

// spec: SWR-PROTO-0005
const boost::ut::suite<"protobuf-attribute-types-round-trip"> attribute_types = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "every attribute type keeps its CloudEvents type through the wire"_test = [] {
    const auto typed = base_event({.extensions = {
                                       {"flag"_ext, true},
                                       {"off"_ext, false},
                                       {"count"_ext, std::int32_t{-5}},
                                       {"least"_ext, std::numeric_limits<std::int32_t>::min()},
                                       {"most"_ext, std::numeric_limits<std::int32_t>::max()},
                                       {"label"_ext, std::string{"x"}},
                                       {"blob"_ext, ce::binary{std::byte{0}, std::byte{255}}},
                                       {"link"_ext, ce::uri{"https://example.com/a"}},
                                       {"relative"_ext, ce::uri_ref{"../a"}},
                                       {"when"_ext, utc_instant("2018-04-05T17:31:00.123Z")},
                                   }});
    const auto back = round_trip(typed);
    expect(back.has_value() && *back == typed);
  };

  "each type is written in its own arm of the value"_test = [] {
    const auto typed = base_event({.extensions = {
                                       {"a"_ext, true},
                                       {"b"_ext, std::int32_t{-1}},
                                       {"c"_ext, std::string{"s"}},
                                       {"d"_ext, ce::binary{std::byte{9}}},
                                       {"e"_ext, ce::uri{"u:x"}},
                                       {"f"_ext, ce::uri_ref{"r"}},
                                       {"g"_ext, utc_instant("1970-01-01T00:00:00Z")},
                                   }});
    constexpr auto minus_one = std::numeric_limits<std::uint64_t>::max();
    expect(encoded_form(typed) == event_bytes({entry("a", varint(ce_boolean, 1)),
                                               entry("b", varint(ce_integer, minus_one)),
                                               entry("c", text(ce_string, "s")),
                                               entry("d", text(ce_bytes, "\x09"sv)),
                                               entry("e", text(ce_uri, "u:x")),
                                               entry("f", text(ce_uri_ref, "r")),
                                               entry("g", message(ce_timestamp, {}))}));
  };

  "dataschema is read from either URI arm"_test = [] {
    for (const auto arm : {ce_uri, ce_uri_ref}) {
      const auto read = decoded(event_bytes({entry("dataschema", text(arm, "../s"))}));
      expect(read.has_value() && read->dataschema() && read->dataschema()->view() == "../s"sv)
          << arm;
    }
  };

  "an extension is returned in the type of the arm it was read from"_test = [] {
    const auto read = decoded(
        event_bytes({entry("ref", text(ce_uri_ref, "a/b")), entry("abs", text(ce_uri, "a:b"))}));
    expect(read.has_value());
    if (read) {
      expect(std::holds_alternative<ce::uri_ref>(*read->extension("ref")));
      expect(std::holds_alternative<ce::uri>(*read->extension("abs")));
    }
  };

  "the last arm of a value wins"_test = [] {
    const auto read = decoded(
        event_bytes({entry("pick", concat({varint(ce_boolean, 1), text(ce_string, "later")}))}));
    expect(read.has_value() && read->extension("pick") != nullptr);
    if (read) {
      expect(std::get<std::string>(*read->extension("pick")) == "later"sv);
    }
  };

  "a value field of an unknown number or wire type is ignored"_test = [] {
    const auto read = decoded(event_bytes({entry(
        "pick", concat({text(unknown_field, "x"), varint(ce_string, 4), text(ce_string, "ok")}))}));
    expect(read.has_value() && read->extension("pick") != nullptr);
    if (read) {
      expect(std::get<std::string>(*read->extension("pick")) == "ok"sv);
    }
  };
};

// spec: SWR-PROTO-0006
const boost::ut::suite<"protobuf-time-is-an-instant"> time_is_an_instant = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto time_event = [](std::string_view rfc3339) {
    return base_event({.time = utc_instant(rfc3339)});
  };

  "a time is written as the seconds and nanoseconds of its UTC instant"_test = [&] {
    expect(encoded_form(time_event("1970-01-01T00:00:02.000000005Z")) == with_time(2, 5));
  };

  "an offset is folded into the instant and the decoded time is UTC"_test = [&] {
    const auto read = decoded(encoded_form(time_event("2018-04-05T17:31:00+02:00")));
    expect(read.has_value() && read->time().has_value());
    if (read && read->time()) {
      expect(*read->time() == utc_instant("2018-04-05T15:31:00Z"));
      expect(read->time()->offset == std::chrono::minutes::zero());
      expect(read->time()->form == ce::offset_form::utc_designator);
    }
  };

  "the decoded time has the fewest of 0, 3, 6 and 9 digits that hold the nanoseconds"_test = [] {
    struct digits_case {
      std::uint64_t nanos;
      std::uint8_t digits;
    };
    for (const auto& [nanos, digits] : {digits_case{0, 0},
                                        digits_case{500'000'000, 3},
                                        digits_case{1'000'000, 3},
                                        digits_case{120'000, 6},
                                        digits_case{1'000, 6},
                                        digits_case{100, 9},
                                        digits_case{999'999'999, 9},
                                        digits_case{1, 9}}) {
      const auto read = decoded(with_time(10, nanos));
      expect(read.has_value() && read->time().has_value()) << nanos;
      if (read && read->time()) {
        expect(read->time()->fractional_digits.count() == digits) << nanos;
        expect(read->time()->utc.time_since_epoch() ==
               std::chrono::seconds{10} +
                   std::chrono::nanoseconds{static_cast<std::int64_t>(nanos)})
            << nanos;
      }
    }
  };

  "an instant before the epoch round-trips"_test = [&] {
    const auto before = time_event("1969-12-31T23:59:58.500Z");
    const auto back = round_trip(before);
    expect(back.has_value() && *back == before);
  };

  "the epoch is an empty Timestamp"_test = [&] {
    const auto epoch = time_event("1970-01-01T00:00:00Z");
    expect(encoded_form(epoch) == event_bytes({entry("time", message(ce_timestamp, {}))}));
    const auto back = round_trip(epoch);
    expect(back.has_value() && *back == epoch);
  };

  "the earliest and latest instants of the timestamp type are accepted"_test = [] {
    for (const std::int64_t seconds : {-last_seconds, last_seconds}) {
      const auto read = decoded(with_time(static_cast<std::uint64_t>(seconds), 0));
      expect(read.has_value()) << seconds;
    }
  };
};

// spec: SWR-PROTO-0006
const boost::ut::suite<"protobuf-time-out-of-range-is-refused"> time_refused = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  constexpr auto year_9999 = std::uint64_t{253'402'300'799};
  constexpr auto before_year_1 = static_cast<std::uint64_t>(std::int64_t{-62'135'596'800});

  "a Timestamp the timestamp type cannot hold is invalid_timestamp"_test = [] {
    expect(refused(with_time(year_9999, 0), ce::errc::invalid_timestamp));
    expect(refused(with_time(before_year_1, 0), ce::errc::invalid_timestamp));
    expect(refused(with_time(static_cast<std::uint64_t>(last_seconds + 1), 0),
                   ce::errc::invalid_timestamp));
    expect(refused(with_time(static_cast<std::uint64_t>(-last_seconds - 1), 0),
                   ce::errc::invalid_timestamp));
  };
};

// spec: SWR-PROTO-0006
const boost::ut::suite<"protobuf-time-negative-nanos-is-refused"> time_nanos_refused = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "nanos outside 0 to 999999999 are invalid_timestamp"_test = [] {
    constexpr auto minus_five = static_cast<std::uint64_t>(std::int64_t{-5});
    expect(refused(with_time(1, minus_five), ce::errc::invalid_timestamp));
    expect(refused(with_time(1, 1'000'000'000), ce::errc::invalid_timestamp));
    expect(refused(with_time(1, std::uint64_t{1} << 32U), ce::errc::invalid_timestamp));
    expect(decoded(with_time(1, 999'999'999)).has_value());
  };

  "a Timestamp with an unknown field is read"_test = [] {
    const auto body =
        concat({varint(seconds_field, 3), text(unknown_field, "x"), varint(unknown_field, 1)});
    expect(decoded(event_bytes({entry("time", message(ce_timestamp, body))})).has_value());
  };
};

namespace {

#ifndef CE_INCLUDE_DIR
#error "CE_INCLUDE_DIR must be defined by the build"
#endif

/// The `#include <...>` targets of a header of the library.
[[nodiscard]] auto includes_of(std::string_view relative) -> std::vector<std::string> {
  std::ifstream in{std::string{CE_INCLUDE_DIR} + "/" + std::string{relative}};
  std::vector<std::string> found;
  for (std::string line; std::getline(in, line);) {
    constexpr std::string_view directive = "#include <";
    if (line.starts_with(directive)) {
      found.push_back(line.substr(directive.size(), line.find('>') - directive.size()));
    }
  }
  return found;
}

}  // namespace

// spec: SWR-PROTO-0013
const boost::ut::suite<"protobuf-format-includes-no-third-party-header"> no_third_party = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the headers include the standard library and ce::core headers only"_test = [] {
    for (const auto header : {"cloudevents/format/protobuf_format.hpp"sv,
                              "cloudevents/format/detail/protobuf_wire.hpp"sv}) {
      const auto includes = includes_of(header);
      expect(!includes.empty()) << header;
      for (const auto& included : includes) {
        const bool standard = included.find('.') == std::string::npos;
        const bool core = included.starts_with("cloudevents/") &&
                          !included.starts_with("cloudevents/codec/") &&
                          std::filesystem::exists(std::string{CE_INCLUDE_DIR} + "/" + included);
        expect(standard || core) << header << " includes " << included;
      }
    }
  };

  "nothing generated or compiled is needed: the headers are the whole format"_test = [] {
    const auto includes = includes_of("cloudevents/format/protobuf_format.hpp");
    for (const auto& included : includes) {
      expect(!included.ends_with(".pb.h") && !included.starts_with("google/")) << included;
    }
  };
};

int main() {}
