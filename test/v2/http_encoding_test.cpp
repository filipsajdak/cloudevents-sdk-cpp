#include <boost/ut.hpp>

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/result.hpp>

#include <string>
#include <string_view>

// The two HTTP value policies disagreed about who checks what. literal_values
// refused a control character and ill-formed UTF-8 on the way out;
// percent_encoded_values checked nothing and escaped whatever it was given, so
// the value failed at the RECEIVING peer's decode - reporting the fault to
// whoever did not commit it.

namespace {

using namespace std::string_view_literals;

/// A truncated two-byte sequence: a lead byte with no continuation.
constexpr auto truncated = "\xC3"sv;
/// An overlong encoding of '/', which is a second spelling of an ASCII
/// character and exactly what is_valid_utf8 exists to refuse.
constexpr auto overlong = "\xC0\xAF"sv;
/// A lone high surrogate, which UTF-8 does not admit.
constexpr auto surrogate = "\xED\xA0\x80"sv;

}  // namespace

const boost::ut::suite<"percent-encoding-refuses-ill-formed-utf8-on-encode">
    percent_encode_checks = [] {
  using namespace boost::ut;

  "an ill-formed sequence is refused rather than escaped"_test = [] {
    for (const auto bad : {truncated, overlong, surrogate}) {
      const auto encoded = ce::v2::http::percent_encoded_values::encode(bad);
      expect(!encoded.has_value()) << "should refuse to encode";
      if (!encoded) {
        expect(encoded.error().code == ce::v2::errc::invalid_utf8);
      }
    }
  };

  // The policy's job is still percent-encoding. Refusing more than the rule says
  // would make a conformant value unsendable.
  "well-formed values still encode, including the ones the rule targets"_test = [] {
    const auto space = ce::v2::http::percent_encoded_values::encode("a b"sv);
    expect(space.has_value());
    if (space) {
      expect(*space == "a%20b");
    }

    const auto multibyte = ce::v2::http::percent_encoded_values::encode("\xC3\xA9"sv);
    expect(multibyte.has_value()) << "two-byte e-acute is well formed and must survive";

    const auto plain = ce::v2::http::percent_encoded_values::encode("com.example.order"sv);
    expect(plain.has_value());
    if (plain) {
      expect(*plain == "com.example.order");
    }
  };

  // Both policies now refuse the same input, which is what makes the choice
  // between them a question of wire format rather than of safety.
  "the two policies agree about what is not encodable"_test = [] {
    expect(!ce::v2::http::percent_encoded_values::encode(truncated).has_value());
    expect(!ce::v2::http::literal_values::encode(truncated).has_value());
  };

  // The decode side always checked. The point of this requirement is that a
  // producer hears about it first.
  "decode still refuses what it always refused"_test = [] {
    const auto decoded = ce::v2::http::percent_encoded_values::decode("%C3"sv);
    expect(!decoded.has_value());
  };
};

int main() {}
