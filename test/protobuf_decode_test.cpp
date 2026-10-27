#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "protobuf_support.hpp"

// What the decoder refuses, within which limits, and what the encoder promises about its output.

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;
using namespace ce::test::pb;
using protobuf = ce::protobuf_format;

constexpr auto minus_one = std::numeric_limits<std::uint64_t>::max();
constexpr std::int64_t int32_least = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t int32_most = std::numeric_limits<std::int32_t>::max();

/// A varint field whose value is a 32-bit-wrapped integer, as protobuf-java writes an int32.
[[nodiscard]] auto integer_entry(std::int64_t value) -> ce::binary {
  return entry("n", varint(ce_integer, static_cast<std::uint64_t>(value)));
}

}  // namespace

// spec: SWR-PROTO-0009
const boost::ut::suite<"protobuf-decode-error-codes"> error_codes = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "truncated input is a parse_error"_test = [] {
    const auto whole = event_bytes({string_entry("subject", "s")});
    for (std::size_t length = 1; length < whole.size(); ++length) {
      const ce::binary cut{whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length)};
      const auto read = decoded(cut);
      // A cut at a field boundary is a message with fewer fields, which fails on what it lacks;
      // anywhere else the last field is cut short, which is a parse_error.
      expect(read.has_value() || read.error().code == ce::errc::parse_error ||
             read.error().code == ce::errc::unsupported_spec_version ||
             read.error().code == ce::errc::missing_required_attribute)
          << length;
    }
    ce::binary cut_inside_a_field = event_bytes();
    cut_inside_a_field.pop_back();
    expect(refused(cut_inside_a_field, ce::errc::parse_error));
  };

  "a length beyond the remaining bytes is a parse_error"_test = [] {
    ce::binary overrun;
    wire::put_tag(overrun, id_field, wire::wire_type::length_delimited);
    wire::put_varint(overrun, 100);
    overrun.push_back(std::byte{'x'});
    expect(refused(overrun, ce::errc::parse_error));
    ce::binary enormous;
    wire::put_tag(enormous, id_field, wire::wire_type::length_delimited);
    wire::put_varint(enormous, std::numeric_limits<std::uint64_t>::max());
    expect(refused(enormous, ce::errc::parse_error));
  };

  "a varint of more than ten bytes is a parse_error"_test = [] {
    ce::binary runaway = bytes_of("\x08");
    runaway.insert(runaway.end(), 11, std::byte{0x80});
    expect(refused(runaway, ce::errc::parse_error));
  };

  "a group wire type is a parse_error"_test = [] {
    ce::binary group = event_bytes();
    wire::put_varint(group, (std::uint64_t{unknown_field} << 3U) | 3U);
    expect(refused(group, ce::errc::parse_error));
    ce::binary end_group = event_bytes();
    wire::put_varint(end_group, (std::uint64_t{unknown_field} << 3U) | 4U);
    expect(refused(end_group, ce::errc::parse_error));
  };

  "a field number of zero is a parse_error"_test = [] {
    ce::binary zero = event_bytes();
    wire::put_varint(zero, 0);
    expect(refused(zero, ce::errc::parse_error));
  };

  "an absent or empty id, source or type is missing_required_attribute"_test = [] {
    for (const auto field : {id_field, source_field, type_field}) {
      ce::binary emptied;
      for (const auto other : {id_field, source_field, spec_version_field, type_field}) {
        emptied = concat({emptied,
                          text(other,
                               other == spec_version_field ? "1.0"sv
                               : other == field            ? ""sv
                                                           : "v"sv)});
      }
      expect(refused(emptied, ce::errc::missing_required_attribute)) << field;
    }
    for (const auto field : {id_field, source_field, type_field}) {
      ce::binary omitted;
      for (const auto other : {id_field, source_field, spec_version_field, type_field}) {
        if (other != field) {
          omitted = concat({omitted, text(other, other == spec_version_field ? "1.0"sv : "v"sv)});
        }
      }
      expect(refused(omitted, ce::errc::missing_required_attribute)) << field;
    }
  };

  "a spec_version other than 1.0 is unsupported_spec_version"_test = [] {
    for (const auto version : {"0.3"sv, "2.0"sv, ""sv, "1.0 "sv}) {
      expect(refused(concat({text(id_field, "1"),
                             text(source_field, "/s"),
                             text(spec_version_field, version),
                             text(type_field, "t")}),
                     ce::errc::unsupported_spec_version))
          << version;
    }
    expect(refused(concat({text(id_field, "1"), text(source_field, "/s"), text(type_field, "t")}),
                   ce::errc::unsupported_spec_version));
  };

  "a string that is not UTF-8 is invalid_utf8"_test = [] {
    const auto bad = "\xFF"sv;
    expect(refused(concat({text(id_field, bad),
                           text(source_field, "/s"),
                           text(spec_version_field, "1.0"),
                           text(type_field, "t")}),
                   ce::errc::invalid_utf8));
    expect(refused(event_bytes({string_entry("label", bad)}), ce::errc::invalid_utf8));
    expect(refused(event_bytes({entry("label", text(ce_uri, bad))}), ce::errc::invalid_utf8));
    expect(refused(event_bytes({message(attributes_field, text(key_field, bad))}),
                   ce::errc::invalid_utf8));
    expect(refused(event_bytes({text(text_data_field, bad)}), ce::errc::invalid_utf8));
    expect(decoded(event_bytes({text(binary_data_field, bad)})).has_value());
    expect(decoded(event_bytes({entry("blob", text(ce_bytes, bad))})).has_value());
  };

  "a ce_integer outside the range of int32 is out_of_range"_test = [] {
    expect(refused(event_bytes({integer_entry(int32_most + 1)}), ce::errc::out_of_range));
    expect(refused(event_bytes({integer_entry(int32_least - 1)}), ce::errc::out_of_range));
    expect(refused(event_bytes({entry("n", varint(ce_integer, std::uint64_t{1} << 32U))}),
                   ce::errc::out_of_range));
    expect(decoded(event_bytes({integer_entry(int32_most)})).has_value());
    expect(decoded(event_bytes({integer_entry(int32_least)})).has_value());
    expect(decoded(event_bytes({entry("n", varint(ce_integer, minus_one))})).has_value());
  };

  "an attribute value with no arm set is invalid_attribute_value"_test = [] {
    expect(refused(event_bytes({entry("n", {})}), ce::errc::invalid_attribute_value));
    expect(refused(event_bytes({message(attributes_field, text(key_field, "n"))}),
                   ce::errc::invalid_attribute_value));
    expect(refused(event_bytes({entry("n", text(unknown_field, "x"))}),
                   ce::errc::invalid_attribute_value));
  };

  "a context attribute in the wrong arm is type_mismatch"_test = [] {
    expect(refused(event_bytes({string_entry("time", "2018-04-05T17:31:00Z")}),
                   ce::errc::type_mismatch));
    expect(refused(event_bytes({entry("datacontenttype", varint(ce_integer, 1))}),
                   ce::errc::type_mismatch));
    expect(
        refused(event_bytes({entry("subject", varint(ce_integer, 1))}), ce::errc::type_mismatch));
    expect(refused(event_bytes({string_entry("dataschema", "https://example.com/s")}),
                   ce::errc::type_mismatch));
    expect(refused(event_bytes({entry("datacontenttype", text(ce_uri, "text/plain"))}),
                   ce::errc::type_mismatch));
  };

  "an invalid attribute name or a reserved one keeps its own code"_test = [] {
    expect(refused(event_bytes({string_entry("Bad-Name", "x")}), ce::errc::invalid_attribute_name));
    expect(refused(event_bytes({string_entry("", "x")}), ce::errc::invalid_attribute_name));
    for (const auto reserved : {"id"sv, "source"sv, "specversion"sv, "type"sv, "data"sv}) {
      expect(refused(event_bytes({string_entry(reserved, "x")}), ce::errc::reserved_attribute_name))
          << reserved;
    }
  };

  "a present but empty context attribute is refused by its own rule"_test = [] {
    expect(refused(event_bytes({string_entry("subject", "")}), ce::errc::invalid_attribute_value));
    expect(refused(event_bytes({string_entry("datacontenttype", "not a media type")}),
                   ce::errc::invalid_content_type));
  };

  "an unknown field is skipped and the last of duplicate map keys wins"_test = [] {
    const auto read = decoded(event_bytes({string_entry("subject", "first"),
                                           text(unknown_field, "x"),
                                           string_entry("subject", "second")}));
    expect(read.has_value() && read->subject() && read->subject()->view() == "second"sv);
  };

  "a duplicated key is replaced even when its first value was in the wrong arm"_test = [] {
    const auto read = decoded(event_bytes(
        {entry("time", varint(ce_integer, 1)), entry("time", message(ce_timestamp, {}))}));
    expect(read.has_value() && read->time().has_value());
  };

  "empty input has no spec_version, which is not 1.0"_test = [] {
    expect(refused({}, ce::errc::unsupported_spec_version));
  };

  "a fixed-width field cut short at the end of an event is a parse_error"_test = [] {
    for (const auto kind : {wire::wire_type::fixed32, wire::wire_type::fixed64}) {
      ce::binary cut = event_bytes();
      wire::put_tag(cut, unknown_field, kind);
      cut.push_back(std::byte{0});
      expect(refused(cut, ce::errc::parse_error));
    }
  };

  "a message nested in the event that is cut short is a parse_error at every level"_test = [] {
    const auto cut_varint = bytes_of("\x08\x80");
    const auto cut_length = bytes_of(
        "\x0A\x05"
        "a");
    for (const auto& broken : {cut_varint, cut_length}) {
      expect(refused(event_bytes({entry("time", message(ce_timestamp, broken))}),
                     ce::errc::parse_error));
      expect(refused(event_bytes({entry("n", broken)}), ce::errc::parse_error));
      expect(refused(event_bytes({message(attributes_field, broken)}), ce::errc::parse_error));
      expect(refused(event_bytes({message(proto_data_field, broken)}), ce::errc::parse_error));
    }
  };

  "a map entry reads its key and value in the wire types they have and skips the rest"_test = [] {
    const auto read =
        decoded(event_bytes({message(attributes_field,
                                     concat({varint(key_field, 5),
                                             text(key_field, "n"),
                                             varint(value_field, 5),
                                             text(unknown_field, "x"),
                                             message(value_field, text(ce_string, "v"))}))}));
    expect(read.has_value() && read->extension("n") != nullptr);
  };

  "a fixed-width field inside a nested message is skipped"_test = [] {
    ce::binary fixed32;
    wire::put_tag(fixed32, unknown_field, wire::wire_type::fixed32);
    fixed32.insert(fixed32.end(), 4, std::byte{0});
    ce::binary fixed64;
    wire::put_tag(fixed64, unknown_field, wire::wire_type::fixed64);
    fixed64.insert(fixed64.end(), 8, std::byte{0});
    for (const auto& skipped : {fixed32, fixed64}) {
      const auto read = decoded(event_bytes(
          {entry("time", message(ce_timestamp, concat({skipped, varint(seconds_field, 1)}))),
           message(attributes_field,
                   concat({skipped,
                           text(key_field, "n"),
                           message(value_field, concat({skipped, text(ce_string, "v")}))})),
           message(proto_data_field, concat({skipped, text(any_value_field, "v")}))}));
      expect(read.has_value() && read->time().has_value() && read->extension("n") != nullptr);
    }
  };

  "source is a field and not an arm: an attribute named source is reserved"_test = [] {
    expect(refused(event_bytes({entry("source", text(ce_uri, "/other"))}),
                   ce::errc::reserved_attribute_name));
    expect(refused(event_bytes({entry("source", text(ce_uri_ref, "/other"))}),
                   ce::errc::reserved_attribute_name));
  };
};

// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-limit-encoded-size"> limit_encoded_size = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "an event above the limit is out_of_range, one at the limit is read"_test = [] {
    const auto bytes = event_bytes();
    expect(decoded(bytes, {.max_encoded_size = bytes.size()}).has_value());
    expect(refused(bytes, ce::errc::out_of_range, {.max_encoded_size = bytes.size() - 1}));
    expect(refused(bytes, ce::errc::out_of_range, {.max_encoded_size = 0}));
  };

  "the size is checked before anything is read, so garbage above the limit is out_of_range"_test =
      [] {
        const ce::binary garbage(64, std::byte{0xFF});
        expect(refused(garbage, ce::errc::out_of_range, {.max_encoded_size = 63}));
        expect(refused(garbage, ce::errc::parse_error));
      };
};

// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-limit-attribute-count"> limit_attribute_count = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto three =
      event_bytes({string_entry("a", "1"), string_entry("b", "2"), string_entry("c", "3")});

  "more attribute entries than the limit is out_of_range, as many is read"_test = [&] {
    expect(decoded(three, {.max_attribute_count = 3}).has_value());
    expect(refused(three, ce::errc::out_of_range, {.max_attribute_count = 2}));
    expect(refused(three, ce::errc::out_of_range, {.max_attribute_count = 0}));
  };

  "the count is checked before an entry is decoded"_test = [] {
    const auto malformed_third = event_bytes({string_entry("a", "1"),
                                              string_entry("b", "2"),
                                              message(attributes_field, bytes_of("\x0A\x7F"))});
    expect(refused(malformed_third, ce::errc::out_of_range, {.max_attribute_count = 2}));
    expect(refused(malformed_third, ce::errc::parse_error, {.max_attribute_count = 3}));
  };

  "repeated keys count as entries, since each one is wire data to read"_test = [&] {
    const auto repeated =
        event_bytes({string_entry("a", "1"), string_entry("a", "2"), string_entry("a", "3")});
    expect(refused(repeated, ce::errc::out_of_range, {.max_attribute_count = 2}));
  };

  "the default limit lets 1024 entries through and refuses 1025"_test = [] {
    ce::binary many = event_bytes();
    for (std::size_t index = 0; index < ce::protobuf::default_max_attribute_count; ++index) {
      many = concat({many, string_entry("a" + std::to_string(index), "x")});
    }
    expect(decoded(many).has_value());
    many = concat({many, string_entry("over", "x")});
    expect(refused(many, ce::errc::out_of_range));
  };
};

// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-fixed-depth"> fixed_depth = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "an unknown field nested far deeper than the schema is skipped without recursion"_test = [] {
    constexpr std::size_t levels = 20'000;
    ce::binary nested = bytes_of("x");
    for (std::size_t level = 0; level < levels; ++level) {
      nested = message(unknown_field, nested);
    }
    expect(decoded(concat({event_bytes(), nested})).has_value());
    expect(decoded(event_bytes({entry("n", nested)})).has_value() == false);
    expect(decoded(event_bytes({entry("time", message(ce_timestamp, nested))})).has_value());
    expect(decoded(event_bytes({message(proto_data_field, nested)})).has_value());
  };

  "a value nested inside a value is not read as one"_test = [] {
    const auto inner = text(ce_string, "x");
    const auto read = decoded(
        event_bytes({entry("n", concat({text(ce_string, "y"), message(unknown_field, inner)}))}));
    expect(read.has_value() && std::get<std::string>(*read->extension("n")) == "y"sv);
  };
};

// spec: SWR-PROTO-0011
const boost::ut::suite<"protobuf-encoding-is-deterministic"> deterministic = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "equal events encode to equal bytes, whatever order the extensions were set in"_test = [] {
    auto forward = base_event();
    auto backward = base_event();
    for (const auto name : {"alpha"_ext, "bravo"_ext, "charlie"_ext}) {
      forward.set_extension(name, std::string{"v"});
    }
    for (const auto name : {"charlie"_ext, "bravo"_ext, "alpha"_ext}) {
      backward.set_extension(name, std::string{"v"});
    }
    expect(encoded_form(forward) == encoded_form(backward));
  };

  "the attributes map is in ascending order of name, context attributes among the extensions"_test =
      [] {
        auto mixed = base_event({
            .datacontenttype = "text/plain"_mediatype,
            .dataschema = "s:x"_dataschema,
            .subject = "s"_subject,
            .time = utc_instant("1970-01-01T00:00:01Z"),
        });
        for (const auto name : {"zzz"_ext, "datab"_ext, "datax"_ext, "sub"_ext, "tim"_ext}) {
          mixed.set_extension(name, true);
        }
        const auto flag = varint(ce_boolean, 1);
        expect(encoded_form(mixed) ==
               event_bytes({entry("datab", flag),
                            string_entry("datacontenttype", "text/plain"),
                            entry("dataschema", text(ce_uri, "s:x")),
                            entry("datax", flag),
                            entry("sub", flag),
                            string_entry("subject", "s"),
                            entry("tim", flag),
                            entry("time", message(ce_timestamp, varint(seconds_field, 1))),
                            entry("zzz", flag)}));
      };

  "the same event encodes to the same bytes every time"_test = [] {
    const auto event = base_event({.extensions = {{"a"_ext, true}, {"b"_ext, std::int32_t{1}}}});
    expect(encoded_form(event) == encoded_form(event));
  };
};

// spec: SWR-PROTO-0012
const boost::ut::suite<"protobuf-decoded-event-reencodes"> reencodes = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "what the decoder accepts, the encoder writes and the decoder reads back equal"_test = [] {
    const std::initializer_list<ce::binary> accepted{
        event_bytes(),
        event_bytes({text(text_data_field, "t")}),
        event_bytes({text(binary_data_field, "")}),
        event_bytes({string_entry("datacontenttype", "text/plain"), text(binary_data_field, "")}),
        event_bytes({message(proto_data_field, any_body("t.example/T", "v"))}),
        event_bytes({message(proto_data_field, any_body("", "v"))}),
        event_bytes({entry("dataschema", text(ce_uri_ref, "../s")),
                     string_entry("datacontenttype", "application/protobuf"),
                     message(proto_data_field, any_body("", "v"))}),
        event_bytes({entry("when", message(ce_timestamp, timestamp_body(5, 120'000))),
                     entry("ref", text(ce_uri_ref, "a")),
                     entry("n", varint(ce_integer, minus_one))}),
        with_time(static_cast<std::uint64_t>(-last_seconds), 999'999'999),
        with_time(static_cast<std::uint64_t>(last_seconds), 999'999'999),
    };
    for (const auto& bytes : accepted) {
      const auto first = decoded(bytes);
      expect(first.has_value());
      if (!first) {
        continue;
      }
      const auto again = round_trip(*first);
      expect(again.has_value() && *again == *first);
    }
  };

  "a binary payload with an unusable type URL as dataschema still round-trips"_test = [] {
    const auto read = decoded(event_bytes({string_entry("datacontenttype", "application/protobuf"),
                                           entry("dataschema", text(ce_uri, "not a uri")),
                                           text(binary_data_field, "v")}));
    expect(read.has_value());
    if (read) {
      const auto again = round_trip(*read);
      expect(again.has_value() && *again == *read);
    }
  };
};

int main() {}
