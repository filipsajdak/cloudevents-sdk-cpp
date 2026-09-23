#include <boost/ut.hpp>

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/v2/format/json_format.hpp>
#include <cloudevents/v2/format/typed_payload.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// One negative test per errc, and a check that the set is complete.
//
// The suite is self-policing: every case records the code it observed, and a
// final test compares that against the enumerator list. Adding an errc without
// a negative test fails here rather than passing silently, which is the whole
// point of the requirement.

namespace {

using namespace std::string_view_literals;

using codec = ce::v2::codec::nlohmann_codec;
using format = ce::v2::json_format<codec>;

/// Every enumerator, in declaration order. `to_string_view` is checked against
/// this too, so an enumerator added without a name is also caught.
constexpr std::array all_codes{
    ce::v2::errc::missing_required_attribute,
    ce::v2::errc::invalid_attribute_name,
    ce::v2::errc::reserved_attribute_name,
    ce::v2::errc::invalid_attribute_value,
    ce::v2::errc::unsupported_spec_version,
    ce::v2::errc::invalid_timestamp,
    ce::v2::errc::invalid_content_type,
    ce::v2::errc::parse_error,
    ce::v2::errc::type_mismatch,
    ce::v2::errc::out_of_range,
    ce::v2::errc::data_conflict,
    ce::v2::errc::invalid_base64,
    ce::v2::errc::invalid_utf8,
    ce::v2::errc::not_a_cloudevent,
    ce::v2::errc::invalid_argument,
};

std::set<ce::v2::errc>& observed() {
  static std::set<ce::v2::errc> codes;
  return codes;
}

/// \brief Assert that `outcome` failed with `expected`, and record it.
template <class R>
void fails_with(const R& outcome, ce::v2::errc expected, std::string_view what) {
  using namespace boost::ut;
  expect(!outcome.has_value()) << what << ": expected a failure";
  if (outcome.has_value()) {
    return;
  }
  expect(outcome.error().code == expected)
      << what << ": expected " << ce::v2::to_string_view(expected) << ", got "
      << ce::v2::to_string_view(outcome.error().code);
  if (outcome.error().code == expected) {
    observed().insert(expected);
  }
}

using namespace ce::v2::literals;

[[nodiscard]] auto minimal(ce::v2::event::options rest = {}) -> ce::v2::event {
  return ce::v2::event{"id-1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// A binary-mode request carrying the four attributes every case needs, plus
/// whatever fields the case is about.
[[nodiscard]] auto request_with(ce::v2::raw_headers::entry extra) -> ce::v2::message {
  return ce::v2::message{.header_fields = {{"ce-specversion", "1.0"},
                                       {"ce-id", "1"},
                                       {"ce-source", "/s"},
                                       {"ce-type", "t"},
                                       std::move(extra)}};
}

struct parcel {
  std::string label;
  std::int32_t weight;
};

CE_DESCRIBE(parcel, label, weight);

const boost::ut::suite<"errc-negative-coverage"> negative_coverage = [] {
  using namespace boost::ut;

  "missing_required_attribute"_test = [] {
    fails_with(format::decode(R"({"specversion":"1.0","source":"/s","type":"t"})"),
               ce::v2::errc::missing_required_attribute, "id absent from a document");
    fails_with(ce::v2::id::make(""sv), ce::v2::errc::missing_required_attribute, "id present but empty");
    fails_with(minimal().get<ce::v2::ext::tracing>(), ce::v2::errc::missing_required_attribute,
               "a required extension field is absent");
    fails_with(ce::v2::data_as<parcel, codec>(minimal()), ce::v2::errc::missing_required_attribute,
               "the event carries no payload");
  };

  "invalid_attribute_name"_test = [] {
    fails_with(ce::v2::extension_name::make("Upper"sv), ce::v2::errc::invalid_attribute_name,
               "an uppercase extension name");
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","has_underscore":"x"})"),
               ce::v2::errc::invalid_attribute_name, "an underscore in a decoded extension name");
    fails_with(ce::v2::http::from_message<codec>(request_with({"ce-has_underscore", "x"})),
               ce::v2::errc::invalid_attribute_name, "an underscore in a ce- header name");
  };

  "reserved_attribute_name"_test = [] {
    fails_with(ce::v2::extension_name::make("type"sv), ce::v2::errc::reserved_attribute_name,
               "an extension redefining type");
    fails_with(ce::v2::extension_name::make("specversion"sv), ce::v2::errc::reserved_attribute_name,
               "an extension redefining specversion");
  };

  "invalid_attribute_value"_test = [] {
    fails_with(ce::v2::subject::make(""sv), ce::v2::errc::invalid_attribute_value,
               "subject present but empty");
    fails_with(ce::v2::dataschema::make(""sv), ce::v2::errc::invalid_attribute_value,
               "dataschema present but empty");

    fails_with(ce::v2::ext::sampled_rate{.sampledrate = 0}.validate(),
               ce::v2::errc::invalid_attribute_value, "a sampled rate of zero");
  };

  "unsupported_spec_version"_test = [] {
    fails_with(format::decode(R"({"specversion":"0.3","id":"1","source":"/s","type":"t"})"),
               ce::v2::errc::unsupported_spec_version, "a 0.3 document");

    fails_with(ce::v2::spec_version::make("2.0"sv), ce::v2::errc::unsupported_spec_version,
               "a 2.0 version");

    const ce::v2::message request{.header_fields = {{"ce-specversion", "0.3"},
                                                {"ce-id", "1"},
                                                {"ce-source", "/s"},
                                                {"ce-type", "t"}}};
    fails_with(ce::v2::http::from_message<codec>(request), ce::v2::errc::unsupported_spec_version,
               "a 0.3 message");
  };

  "invalid_timestamp"_test = [] {
    for (const auto bad : {"not-a-time"sv, "2026-13-01T00:00:00Z"sv, "2026-09-20"sv,
                           "2026-09-20T25:00:00Z"sv, ""sv}) {
      fails_with(ce::v2::parse_timestamp(bad), ce::v2::errc::invalid_timestamp, bad);
    }
  };

  "invalid_content_type"_test = [] {
    fails_with(ce::v2::datacontenttype::make("not a media type"sv), ce::v2::errc::invalid_content_type,
               "a malformed media type");
  };

  "parse_error"_test = [] {
    fails_with(format::decode(R"({"specversion":)"), ce::v2::errc::parse_error, "truncated JSON");
    fails_with(format::decode("[1,2,3]"), ce::v2::errc::parse_error, "a JSON array, not an object");
    fails_with(ce::v2::http::detail::percent_decode("%ZZ"), ce::v2::errc::parse_error,
               "a non-hexadecimal percent escape");
    fails_with(ce::v2::http::detail::percent_decode("abc%4"), ce::v2::errc::parse_error,
               "a truncated percent escape");
  };

  "type_mismatch"_test = [] {
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","rate":1.5})"),
               ce::v2::errc::type_mismatch, "a floating-point extension value");

    const auto subject = minimal(
        {.extensions = {{"sampledrate"_ext, ce::v2::attribute_value{std::string{"not-a-number"}}}}});
    fails_with(subject.get<ce::v2::ext::sampled_rate>(), ce::v2::errc::type_mismatch,
               "an extension string that is not an integer");

    fails_with(ce::v2::data_as<parcel, codec>(minimal({.data = ce::v2::binary{std::byte{0x00}}})),
               ce::v2::errc::type_mismatch, "a binary payload read as a described type");

    const auto wrong = minimal({.datacontenttype = "application/json"_mediatype,
                                .data = ce::v2::json_text{.raw = R"({"label":7,"weight":1})"}});
    fails_with(ce::v2::data_as<parcel, codec>(wrong), ce::v2::errc::type_mismatch,
               "a payload member of the wrong JSON type");
  };

  "out_of_range"_test = [] {
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":9999999999})"),
               ce::v2::errc::out_of_range, "an extension integer past the Integer range");

    const auto huge =
        minimal({.datacontenttype = "application/json"_mediatype,
                 .data = ce::v2::json_text{.raw = R"({"label":"x","weight":2147483648})"}});
    fails_with(ce::v2::data_as<parcel, codec>(huge), ce::v2::errc::out_of_range,
               "a payload integer past its declared field");
  };

  "data_conflict"_test = [] {
    fails_with(
        format::decode(
            R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":1,"data_base64":"AA=="})"),
        ce::v2::errc::data_conflict, "both data members present");
  };

  "invalid_base64"_test = [] {
    for (const auto bad : {"A"sv, "AAAAA"sv, "A!=="sv, "AB=A"sv}) {
      fails_with(ce::v2::base64_decode(bad), ce::v2::errc::invalid_base64, bad);
    }
    fails_with(
        format::decode(
            R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"!!!!"})"),
        ce::v2::errc::invalid_base64, "a malformed data_base64 member");
  };

  "invalid_utf8"_test = [] {
    // An overlong encoding of '/', which is a second spelling of the same text.
    fails_with(ce::v2::http::from_message<codec>(request_with({"ce-subject", "%C0%AF"})),
               ce::v2::errc::invalid_utf8, "an overlong UTF-8 sequence in a header");

    // A lone surrogate, which no well-formed UTF-8 carries.
    fails_with(ce::v2::http::from_message<codec>(request_with({"ce-subject", "%ED%A0%80"})),
               ce::v2::errc::invalid_utf8, "a surrogate in a header");
  };

  "not_a_cloudevent"_test = [] {
    const ce::v2::message plain{.header_fields = {{"Content-Type", "application/json"}},
                            .body = ce::v2::http::detail::to_bytes(R"({"hello":"world"})")};
    fails_with(ce::v2::http::from_message<codec>(plain), ce::v2::errc::not_a_cloudevent,
               "a request with no ce- headers");

    const ce::v2::message bare{};
    fails_with(ce::v2::http::from_message<codec>(bare), ce::v2::errc::not_a_cloudevent,
               "a request with no headers at all");
  };

  "invalid_argument"_test = [] {
    fails_with(ce::v2::http::to_message<codec>(minimal(), ce::v2::content_mode::batched),
               ce::v2::errc::invalid_argument, "asking the single-event encoder for a batch");

    auto batch = ce::v2::http::to_batch_message<codec>(std::span<const ce::v2::event>{});
    expect(batch.has_value());
    if (batch) {
      fails_with(ce::v2::http::from_message<codec>(*batch), ce::v2::errc::invalid_argument,
                 "asking the single-event decoder for a batch message");
    }
  };

  // This test must run last, so it is named to sort after the others in the
  // order ut registers them within a suite: it reads what the cases recorded.
  "every errc has a negative test"_test = [] {
    for (const auto code : all_codes) {
      expect(observed().contains(code))
          << "no negative test presented input producing " << ce::v2::to_string_view(code);
    }
    expect(observed().size() == all_codes.size())
        << "a negative test produced a code not in the enumerator list";
  };

  "every errc has a name"_test = [] {
    // An enumerator added without a case in to_string_view would report
    // "unknown" here, which is how a diagnostic goes quietly missing.
    for (const auto code : all_codes) {
      expect(ce::v2::to_string_view(code) != "unknown"sv)
          << "errc value " << static_cast<int>(code) << " has no name";
      expect(!ce::v2::to_string_view(code).empty());
    }
  };

  "the enumerator list is the whole enum"_test = [] {
    // The list above is hand-written, so it needs its own check: the last
    // enumerator's value must equal the list length, which fails the moment one
    // is added or removed without the list being updated.
    static_assert(static_cast<int>(ce::v2::errc::missing_required_attribute) == 1);
    static_assert(static_cast<int>(ce::v2::errc::invalid_argument) ==
                  static_cast<int>(all_codes.size()));
    expect(true);
  };
};

}  // namespace

int main() {}
