#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/detail/protobuf_wire.hpp>
#include <cloudevents/result.hpp>

// The wire layer under the Protobuf format: varints, field headers and lengths read
// from bytes a peer chose. Each refusal is named here because a hand-written parser
// fails in exactly these places.

namespace {

namespace wire = ce::protobuf::detail;
using namespace std::string_view_literals;

constexpr auto last_field_number = static_cast<std::uint32_t>(wire::max_field_number);

[[nodiscard]] auto bytes_of(std::initializer_list<unsigned> octets) -> ce::binary {
  ce::binary out;
  for (const unsigned octet : octets) {
    out.push_back(static_cast<std::byte>(octet));
  }
  return out;
}

struct varint_vector {
  std::uint64_t value;
  ce::binary encoded;
};

[[nodiscard]] auto varint_vectors() -> std::array<varint_vector, 7> {
  return {{
      {0, bytes_of({0x00})},
      {1, bytes_of({0x01})},
      {127, bytes_of({0x7F})},
      {128, bytes_of({0x80, 0x01})},
      {300, bytes_of({0xAC, 0x02})},
      {std::uint64_t{1} << 32U, bytes_of({0x80, 0x80, 0x80, 0x80, 0x10})},
      {std::numeric_limits<std::uint64_t>::max(),
       bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01})},
  }};
}

[[nodiscard]] auto read_one_varint(const ce::binary& encoded) -> ce::result<std::uint64_t> {
  wire::reader in{encoded};
  return in.read_varint();
}

[[nodiscard]] auto fails_with(const ce::result<std::uint64_t>& read, ce::errc code) -> bool {
  return !read.has_value() && read.error().code == code;
}

}  // namespace

// spec: SWR-PROTO-0009
const boost::ut::suite<"protobuf-wire-varint"> varints = [] {
  using namespace boost::ut;

  "a varint round-trips through the writer and the reader"_test = [] {
    for (const auto& [value, encoded] : varint_vectors()) {
      ce::binary written;
      wire::put_varint(written, value);
      expect(written == encoded) << value;
      const auto read = read_one_varint(encoded);
      expect(read.has_value() && *read == value) << value;
    }
  };

  "a varint cut short is a parse error"_test = [] {
    expect(fails_with(read_one_varint(bytes_of({0x80})), ce::errc::parse_error));
    expect(fails_with(read_one_varint({}), ce::errc::parse_error));
  };

  "a varint of more than ten bytes is a parse error"_test = [] {
    expect(fails_with(read_one_varint(bytes_of(
                          {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01})),
                      ce::errc::parse_error));
  };

  "a tenth byte above 1 does not fit 64 bits"_test = [] {
    expect(fails_with(
        read_one_varint(bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02})),
        ce::errc::parse_error));
  };

  "a non-minimal varint is read, not refused"_test = [] {
    const auto read = read_one_varint(bytes_of({0x81, 0x80, 0x00}));
    expect(read.has_value() && *read == 1U);
  };
};

// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-wire-named-limits"> limits = [] {
  using namespace boost::ut;

  "the varint bound is the wire format's own ten bytes"_test = [] {
    expect(ce::protobuf::max_varint_bytes == 10U);
  };
};

// spec: SWR-PROTO-0009
const boost::ut::suite<"protobuf-wire-field-headers"> headers = [] {
  using namespace boost::ut;

  "a header splits into a field number and a wire type"_test = [] {
    const auto encoded = bytes_of({0x32});  // field 6, length-delimited
    wire::reader in{encoded};
    const auto header = in.read_header();
    expect(header.has_value());
    if (header) {
      expect(header->number == 6U);
      expect(header->kind == wire::wire_type::length_delimited);
    }
  };

  "the writer's tag reads back as the same header"_test = [] {
    for (const auto kind : {wire::wire_type::varint,
                            wire::wire_type::fixed64,
                            wire::wire_type::length_delimited,
                            wire::wire_type::fixed32}) {
      ce::binary written;
      wire::put_tag(written, last_field_number, kind);
      wire::reader in{written};
      const auto header = in.read_header();
      expect(header.has_value() && header->number == last_field_number && header->kind == kind);
    }
  };

  "field number zero is a parse error"_test = [] {
    const auto encoded = bytes_of({0x02});
    wire::reader in{encoded};
    const auto header = in.read_header();
    expect(!header.has_value() && header.error().code == ce::errc::parse_error);
  };

  "a field number above 29 bits is a parse error"_test = [] {
    ce::binary written;
    wire::put_varint(written, std::uint64_t{1} << 32U);
    wire::reader in{written};
    const auto header = in.read_header();
    expect(!header.has_value() && header.error().code == ce::errc::parse_error);
  };

  "a group or a reserved wire type is a parse error"_test = [] {
    for (const unsigned tag_byte : {0x0BU, 0x0CU, 0x0EU, 0x0FU}) {
      const auto encoded = bytes_of({tag_byte});
      wire::reader in{encoded};
      const auto header = in.read_header();
      expect(!header.has_value() && header.error().code == ce::errc::parse_error) << tag_byte;
    }
  };

  "a header cut short is a parse error"_test = [] {
    const auto encoded = bytes_of({0x80});
    wire::reader in{encoded};
    expect(!in.read_header().has_value());
  };
};

// spec: SWR-PROTO-0009
const boost::ut::suite<"protobuf-wire-lengths-and-skipping"> lengths = [] {
  using namespace boost::ut;

  "a length-delimited body is the bytes after its length"_test = [] {
    const auto encoded = bytes_of({0x02, 'h', 'i', 0x07});
    wire::reader in{encoded};
    const auto body = in.read_length_delimited();
    expect(body.has_value() && wire::text_view(*body) == "hi"sv);
    expect(!in.done());
  };

  "a length beyond the remaining bytes is a parse error"_test = [] {
    const auto encoded = bytes_of({0x03, 'h', 'i'});
    wire::reader in{encoded};
    const auto body = in.read_length_delimited();
    expect(!body.has_value() && body.error().code == ce::errc::parse_error);
  };

  "a length of 2^64 - 1 does not overflow the bounds check"_test = [] {
    ce::binary encoded;
    wire::put_varint(encoded, std::numeric_limits<std::uint64_t>::max());
    wire::reader in{encoded};
    const auto body = in.read_length_delimited();
    expect(!body.has_value() && body.error().code == ce::errc::parse_error);
  };

  "a length with no bytes behind it is a parse error"_test = [] {
    const auto encoded = bytes_of({0x80});
    wire::reader in{encoded};
    expect(!in.read_length_delimited().has_value());
  };

  "a fixed-width field cut short is a parse error"_test = [] {
    const auto encoded = bytes_of({0x01, 0x02, 0x03});
    wire::reader in{encoded};
    const auto skipped = in.skip(wire::wire_type::fixed32);
    expect(!skipped.has_value() && skipped.error().code == ce::errc::parse_error);
  };

  "skipping a fixed32, a fixed64 and a length-delimited field leaves the rest"_test = [] {
    const auto encoded = bytes_of({1, 2, 3, 4, 1, 2, 3, 4, 5, 6, 7, 8, 0x02, 'h', 'i', 0x7F});
    wire::reader in{encoded};
    expect(in.skip(wire::wire_type::fixed32).has_value());
    expect(in.skip(wire::wire_type::fixed64).has_value());
    expect(in.skip(wire::wire_type::length_delimited).has_value());
    expect(!in.done());
    expect(in.skip(wire::wire_type::varint).has_value());
    expect(in.done());
  };

  "skipping a varint or a length that is cut short is a parse error"_test = [] {
    const auto cut_varint = bytes_of({0x80});
    wire::reader varint_in{cut_varint};
    expect(!varint_in.skip(wire::wire_type::varint).has_value());
    const auto cut_length = bytes_of({0x05, 'h'});
    wire::reader length_in{cut_length};
    expect(!length_in.skip(wire::wire_type::length_delimited).has_value());
  };

  "string bytes that are not UTF-8 are refused with invalid_utf8"_test = [] {
    const auto lone_continuation = bytes_of({0x80});
    const auto text = wire::read_text(lone_continuation);
    expect(!text.has_value() && text.error().code == ce::errc::invalid_utf8);
    const auto fine = bytes_of({'o', 'k'});
    expect(wire::read_text(fine).has_value());
  };
};

// spec: SWR-PROTO-0003
const boost::ut::suite<"protobuf-wire-writer"> writer = [] {
  using namespace boost::ut;

  "a length-delimited field is its tag, its length and its body"_test = [] {
    ce::binary written;
    wire::put_field(written, 5, "hi"sv);
    expect(written == bytes_of({0x2A, 0x02, 'h', 'i'}));
  };

  "a varint field is its tag and its value"_test = [] {
    ce::binary written;
    wire::put_varint_field(written, 2, 150);
    expect(written == bytes_of({0x10, 0x96, 0x01}));
  };
};

int main() {}
