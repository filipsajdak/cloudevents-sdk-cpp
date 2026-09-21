#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// base64 is the one place the JSON layer turns octets into text, and the text
// arrives from an untrusted peer on the decode side. So the vectors, the
// rejections and the padding rule are all checked here, and the constexpr
// promise is checked with static_assert: a base64 that only works at run time
// has already broken the contract SWR-JSON-0027 states.

namespace {

using namespace std::string_view_literals;

[[nodiscard]] constexpr auto bytes_of(std::string_view text) -> ce::binary {
  ce::binary bytes;
  bytes.reserve(text.size());
  for (const char character : text) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return bytes;
}

[[nodiscard]] auto text_of(const ce::binary& bytes) -> std::string {
  std::string text;
  text.reserve(bytes.size());
  for (const auto octet : bytes) {
    text.push_back(static_cast<char>(octet));
  }
  return text;
}

/// True when `plain` encodes to exactly `encoded`. Constexpr so the same
/// expectation can be asserted at compile time and at run time.
[[nodiscard]] constexpr auto encodes_to(std::string_view plain, std::string_view encoded) -> bool {
  return ce::base64_encode(bytes_of(plain)) == encoded;
}

/// True when `encoded` decodes to exactly the octets of `plain`.
[[nodiscard]] constexpr auto decodes_to(std::string_view encoded, std::string_view plain) -> bool {
  const auto decoded = ce::base64_decode(encoded);
  return decoded.has_value() && *decoded == bytes_of(plain);
}

/// Not constexpr: `ce::fail` is not, so the rejection path of base64_decode
/// cannot be constant-evaluated. The success path can, which is what
/// SWR-JSON-0027 asks of the header.
[[nodiscard]] auto rejected(std::string_view encoded) -> bool {
  const auto decoded = ce::base64_decode(encoded);
  return !decoded.has_value() && decoded.error().code == ce::errc::invalid_base64;
}

/// The RFC 4648 section 10 vectors, which exercise every padding length of the
/// section 4 alphabet.
constexpr std::pair<std::string_view, std::string_view> rfc_vectors[] = {
    {""sv, ""sv},         {"f"sv, "Zg=="sv},         {"fo"sv, "Zm8="sv},
    {"foo"sv, "Zm9v"sv},  {"foob"sv, "Zm9vYg=="sv},  {"fooba"sv, "Zm9vYmE="sv},
    {"foobar"sv, "Zm9vYmFy"sv},
};

// spec: SWR-JSON-0027
const boost::ut::suite<"base64-rfc4648-section-4-vectors"> base64_vectors = [] {
  using namespace boost::ut;

  "the RFC 4648 section 10 vectors round-trip"_test = [] {
    for (const auto& [plain, encoded] : rfc_vectors) {
      expect(ce::base64_encode(bytes_of(plain)) == std::string{encoded})
          << "encoding " << plain;
      const auto decoded = ce::base64_decode(encoded);
      expect(decoded.has_value()) << "decoding " << encoded;
      if (decoded) {
        expect(text_of(*decoded) == std::string{plain}) << "decoding " << encoded;
      }
    }
  };

  // A run-time-only base64 could not be used to build a compile-time constant,
  // which is what SWR-JSON-0027 requires of the header.
  "encode and decode are usable in a constant expression"_test = [] {
    static_assert(encodes_to(""sv, ""sv));
    static_assert(encodes_to("f"sv, "Zg=="sv));
    static_assert(encodes_to("fo"sv, "Zm8="sv));
    static_assert(encodes_to("foo"sv, "Zm9v"sv));
    static_assert(encodes_to("foob"sv, "Zm9vYg=="sv));
    static_assert(encodes_to("fooba"sv, "Zm9vYmE="sv));
    static_assert(encodes_to("foobar"sv, "Zm9vYmFy"sv));
    static_assert(decodes_to("Zm9vYmFy"sv, "foobar"sv));
    static_assert(decodes_to("Zm9vYmE="sv, "fooba"sv));
    static_assert(decodes_to(""sv, ""sv));
    // The rejection path is checked at run time instead: it goes through
    // ce::fail, which is not constexpr.
    expect(rejected("Zm9v!"sv));
  };

  // The section 4 alphabet, not the URL-safe section 5 one: '+' and '/' are the
  // last two characters, and a peer sending '-' or '_' is sending section 5.
  "the standard alphabet is used, not the URL-safe one"_test = [] {
    const ce::binary high{std::byte{0xFB}, std::byte{0xF0}};
    expect(ce::base64_encode(high) == std::string{"+/A="}) << ce::base64_encode(high);
    expect(!ce::base64_decode("Zm9-"sv).has_value());
    expect(!ce::base64_decode("Zm9_"sv).has_value());
  };

  "every byte value survives a round-trip, including NUL"_test = [] {
    ce::binary all;
    for (int value = 0; value < 256; ++value) {
      all.push_back(static_cast<std::byte>(value));
    }
    const auto decoded = ce::base64_decode(ce::base64_encode(all));
    expect(decoded.has_value());
    if (decoded) {
      expect(*decoded == all);
    }
  };
};

// spec: SWR-JSON-0028
// spec: SWR-JSON-0038
const boost::ut::suite<"base64-decode-rejects-invalid-input"> base64_rejects_invalid = [] {
  using namespace boost::ut;

  "a character outside the alphabet is rejected"_test = [] {
    for (const auto bad : {"Zg=?"sv, "Zm9v!"sv, "Z m8"sv, "Zg\n"sv, "Zm9-"sv, "Zm9_"sv}) {
      const auto decoded = ce::base64_decode(bad);
      expect(!decoded.has_value()) << "should reject " << bad;
      if (!decoded) {
        expect(decoded.error().code == ce::errc::invalid_base64);
      }
    }
  };

  "a length no octet sequence can produce is rejected"_test = [] {
    // One leftover sextet carries six bits, and no whole number of octets
    // encodes to that.
    for (const auto bad : {"A"sv, "ZmFy_w"sv, "ZmFyA"sv}) {
      const auto decoded = ce::base64_decode(bad);
      expect(!decoded.has_value()) << "should reject " << bad;
      if (!decoded) {
        expect(decoded.error().code == ce::errc::invalid_base64);
      }
    }
  };

  // RFC 4648 section 4 pads a final quantum out to four characters, so two is
  // the most any well-formed encoding carries. A decoder that stripped whatever
  // run of '=' it found accepted "QQ======" and "====", which is the same
  // several-spellings-of-one-value problem as the trailing-bits rule below.
  "more than two padding characters are rejected"_test = [] {
    expect(rejected("QQ======"sv));
    expect(rejected("===="sv));
    expect(rejected("Zg==="sv));
    expect(rejected("Zm9v===="sv));
    expect(rejected("A==="sv));
  };

  // Padding is optional, but a spelling that carries it must carry the right
  // amount: "Zg=" is neither the padded form nor the bare one.
  "padding that does not complete the final quantum is rejected"_test = [] {
    expect(rejected("Zg="sv));
    expect(rejected("Zm9vYg="sv));
    expect(rejected("Zm8=="sv));
    // The two spellings that are well formed stay accepted.
    expect(ce::base64_decode("Zg=="sv).has_value());
    expect(ce::base64_decode("Zg"sv).has_value());
    expect(ce::base64_decode("Zm8="sv).has_value());
    expect(ce::base64_decode("Zm8"sv).has_value());
  };

  // Accepting these would make two spellings decode to the same octets, and two
  // peers comparing the encoded forms would disagree with two peers comparing
  // the decoded ones.
  "non-zero unused trailing bits are rejected"_test = [] {
    expect(rejected("Zh"sv));
    expect(rejected("Zm9vYh"sv));
    // The canonical spelling of the same octets is accepted, so the check above
    // is about the leftover bits and not about the length.
    expect(ce::base64_decode("Zg"sv).has_value());
    expect(ce::base64_decode("Zm9vYg"sv).has_value());
  };
};

// spec: SWR-JSON-0029
const boost::ut::suite<"base64-decode-accepts-missing-padding"> base64_accepts_unpadded = [] {
  using namespace boost::ut;

  "an unpadded spelling decodes to the same octets as the padded one"_test = [] {
    const std::pair<std::string_view, std::string_view> pairs[] = {
        {"Zg=="sv, "Zg"sv},
        {"Zm8="sv, "Zm8"sv},
        {"Zm9vYg=="sv, "Zm9vYg"sv},
        {"Zm9vYmE="sv, "Zm9vYmE"sv},
    };
    for (const auto& [padded, bare] : pairs) {
      const auto from_padded = ce::base64_decode(padded);
      const auto from_bare = ce::base64_decode(bare);
      expect(from_padded.has_value()) << padded;
      expect(from_bare.has_value()) << bare;
      if (from_padded && from_bare) {
        expect(*from_padded == *from_bare) << bare << " differs from " << padded;
      }
    }
  };

  "the unpadded forms of the RFC vectors carry the same octets"_test = [] {
    for (const auto& [plain, encoded] : rfc_vectors) {
      auto bare = encoded;
      while (!bare.empty() && bare.back() == '=') {
        bare.remove_suffix(1);
      }
      const auto decoded = ce::base64_decode(bare);
      expect(decoded.has_value()) << "decoding unpadded " << bare;
      if (decoded) {
        expect(text_of(*decoded) == std::string{plain}) << "decoding unpadded " << bare;
      }
    }
  };
};

}  // namespace

int main() {}
