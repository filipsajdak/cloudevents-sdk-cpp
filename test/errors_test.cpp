#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
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

using codec = ce::codec::nlohmann_codec;
using format = ce::json_format<codec>;

/// Every enumerator, in declaration order. `to_string_view` is checked against
/// this too, so an enumerator added without a name is also caught.
constexpr std::array all_codes{
    ce::errc::missing_required_attribute,
    ce::errc::invalid_attribute_name,
    ce::errc::reserved_attribute_name,
    ce::errc::invalid_attribute_value,
    ce::errc::unsupported_spec_version,
    ce::errc::invalid_timestamp,
    ce::errc::invalid_content_type,
    ce::errc::parse_error,
    ce::errc::type_mismatch,
    ce::errc::out_of_range,
    ce::errc::data_conflict,
    ce::errc::invalid_base64,
    ce::errc::invalid_utf8,
    ce::errc::not_a_cloudevent,
    ce::errc::invalid_argument,
};

std::set<ce::errc>& observed() {
  static std::set<ce::errc> codes;
  return codes;
}

/// \brief Assert that `outcome` failed with `expected`, and record it.
template <class R>
void fails_with(const R& outcome, ce::errc expected, std::string_view what) {
  using namespace boost::ut;
  expect(!outcome.has_value()) << what << ": expected a failure";
  if (outcome.has_value()) {
    return;
  }
  expect(outcome.error().code == expected)
      << what << ": expected " << ce::to_string_view(expected) << ", got "
      << ce::to_string_view(outcome.error().code);
  if (outcome.error().code == expected) {
    observed().insert(expected);
  }
}

using namespace ce::literals;

[[nodiscard]] auto minimal(ce::event::options rest = {}) -> ce::event {
  return ce::event{"id-1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// A binary-mode request carrying the four attributes every case needs, plus
/// whatever fields the case is about.
[[nodiscard]] auto request_with(ce::raw_headers::entry extra) -> ce::message {
  return ce::message{.header_fields = {{"ce-specversion", "1.0"},
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

// spec: SWR-SEC-0003
const boost::ut::suite<"errc-negative-coverage"> negative_coverage = [] {
  using namespace boost::ut;

  "missing_required_attribute"_test = [] {
    fails_with(format::decode(R"({"specversion":"1.0","source":"/s","type":"t"})"),
               ce::errc::missing_required_attribute, "id absent from a document");
    fails_with(ce::id::make(""sv), ce::errc::missing_required_attribute, "id present but empty");
    fails_with(minimal().get<ce::ext::tracing>(), ce::errc::missing_required_attribute,
               "a required extension field is absent");
    fails_with(ce::data_as<parcel, codec>(minimal()), ce::errc::missing_required_attribute,
               "the event carries no payload");
  };

  "invalid_attribute_name"_test = [] {
    fails_with(ce::extension_name::make("Upper"sv), ce::errc::invalid_attribute_name,
               "an uppercase extension name");
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","has_underscore":"x"})"),
               ce::errc::invalid_attribute_name, "an underscore in a decoded extension name");
    fails_with(ce::http::from_message<codec>(request_with({"ce-has_underscore", "x"})),
               ce::errc::invalid_attribute_name, "an underscore in a ce- header name");
  };

  "reserved_attribute_name"_test = [] {
    fails_with(ce::extension_name::make("type"sv), ce::errc::reserved_attribute_name,
               "an extension redefining type");
    fails_with(ce::extension_name::make("specversion"sv), ce::errc::reserved_attribute_name,
               "an extension redefining specversion");
  };

  "invalid_attribute_value"_test = [] {
    fails_with(ce::subject::make(""sv), ce::errc::invalid_attribute_value,
               "subject present but empty");
    fails_with(ce::dataschema::make(""sv), ce::errc::invalid_attribute_value,
               "dataschema present but empty");

    fails_with(ce::ext::sampled_rate{.sampledrate = 0}.validate(),
               ce::errc::invalid_attribute_value, "a sampled rate of zero");
  };

  "unsupported_spec_version"_test = [] {
    fails_with(format::decode(R"({"specversion":"0.3","id":"1","source":"/s","type":"t"})"),
               ce::errc::unsupported_spec_version, "a 0.3 document");

    fails_with(ce::spec_version::make("2.0"sv), ce::errc::unsupported_spec_version,
               "a 2.0 version");

    const ce::message request{.header_fields = {{"ce-specversion", "0.3"},
                                                {"ce-id", "1"},
                                                {"ce-source", "/s"},
                                                {"ce-type", "t"}}};
    fails_with(ce::http::from_message<codec>(request), ce::errc::unsupported_spec_version,
               "a 0.3 message");
  };

  "invalid_timestamp"_test = [] {
    for (const auto bad : {"not-a-time"sv, "2026-13-01T00:00:00Z"sv, "2026-09-20"sv,
                           "2026-09-20T25:00:00Z"sv, ""sv}) {
      fails_with(ce::parse_timestamp(bad), ce::errc::invalid_timestamp, bad);
    }
  };

  "invalid_content_type"_test = [] {
    fails_with(ce::datacontenttype::make("not a media type"sv), ce::errc::invalid_content_type,
               "a malformed media type");
  };

  "parse_error"_test = [] {
    fails_with(format::decode(R"({"specversion":)"), ce::errc::parse_error, "truncated JSON");
    fails_with(format::decode("[1,2,3]"), ce::errc::parse_error, "a JSON array, not an object");
    fails_with(ce::http::detail::percent_decode("%ZZ"), ce::errc::parse_error,
               "a non-hexadecimal percent escape");
    fails_with(ce::http::detail::percent_decode("abc%4"), ce::errc::parse_error,
               "a truncated percent escape");
  };

  "type_mismatch"_test = [] {
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","rate":1.5})"),
               ce::errc::type_mismatch, "a floating-point extension value");

    const auto subject = minimal(
        {.extensions = {{"sampledrate"_ext, ce::attribute_value{std::string{"not-a-number"}}}}});
    fails_with(subject.get<ce::ext::sampled_rate>(), ce::errc::type_mismatch,
               "an extension string that is not an integer");

    fails_with(ce::data_as<parcel, codec>(minimal({.data = ce::binary{std::byte{0x00}}})),
               ce::errc::type_mismatch, "a binary payload read as a described type");

    const auto wrong = minimal({.datacontenttype = "application/json"_mediatype,
                                .data = ce::json_text{.raw = R"({"label":7,"weight":1})"}});
    fails_with(ce::data_as<parcel, codec>(wrong), ce::errc::type_mismatch,
               "a payload member of the wrong JSON type");
  };

  "out_of_range"_test = [] {
    fails_with(format::decode(
                   R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":9999999999})"),
               ce::errc::out_of_range, "an extension integer past the Integer range");

    const auto huge =
        minimal({.datacontenttype = "application/json"_mediatype,
                 .data = ce::json_text{.raw = R"({"label":"x","weight":2147483648})"}});
    fails_with(ce::data_as<parcel, codec>(huge), ce::errc::out_of_range,
               "a payload integer past its declared field");
  };

  "data_conflict"_test = [] {
    fails_with(
        format::decode(
            R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":1,"data_base64":"AA=="})"),
        ce::errc::data_conflict, "both data members present");
  };

  "invalid_base64"_test = [] {
    for (const auto bad : {"A"sv, "AAAAA"sv, "A!=="sv, "AB=A"sv}) {
      fails_with(ce::base64_decode(bad), ce::errc::invalid_base64, bad);
    }
    fails_with(
        format::decode(
            R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"!!!!"})"),
        ce::errc::invalid_base64, "a malformed data_base64 member");
  };

  "invalid_utf8"_test = [] {
    // An overlong encoding of '/', which is a second spelling of the same text.
    fails_with(ce::http::from_message<codec>(request_with({"ce-subject", "%C0%AF"})),
               ce::errc::invalid_utf8, "an overlong UTF-8 sequence in a header");

    // A lone surrogate, which no well-formed UTF-8 carries.
    fails_with(ce::http::from_message<codec>(request_with({"ce-subject", "%ED%A0%80"})),
               ce::errc::invalid_utf8, "a surrogate in a header");
  };

  "not_a_cloudevent"_test = [] {
    const ce::message plain{.header_fields = {{"Content-Type", "application/json"}},
                            .body = ce::http::detail::to_bytes(R"({"hello":"world"})")};
    fails_with(ce::http::from_message<codec>(plain), ce::errc::not_a_cloudevent,
               "a request with no ce- headers");

    const ce::message bare{};
    fails_with(ce::http::from_message<codec>(bare), ce::errc::not_a_cloudevent,
               "a request with no headers at all");
  };

  "invalid_argument"_test = [] {
    fails_with(ce::http::to_message<codec>(minimal(), ce::content_mode::batched),
               ce::errc::invalid_argument, "asking the single-event encoder for a batch");

    auto batch = ce::http::to_batch_message<codec>(std::span<const ce::event>{});
    expect(batch.has_value());
    if (batch) {
      fails_with(ce::http::from_message<codec>(*batch), ce::errc::invalid_argument,
                 "asking the single-event decoder for a batch message");
    }
  };

  // This test must run last, so it is named to sort after the others in the
  // order ut registers them within a suite: it reads what the cases recorded.
  "every errc has a negative test"_test = [] {
    for (const auto code : all_codes) {
      expect(observed().contains(code))
          << "no negative test presented input producing " << ce::to_string_view(code);
    }
    expect(observed().size() == all_codes.size())
        << "a negative test produced a code not in the enumerator list";
  };

  "every errc has a name"_test = [] {
    // An enumerator added without a case in to_string_view would report
    // "unknown" here, which is how a diagnostic goes quietly missing.
    for (const auto code : all_codes) {
      expect(ce::to_string_view(code) != "unknown"sv)
          << "errc value " << static_cast<int>(code) << " has no name";
      expect(!ce::to_string_view(code).empty());
    }
  };

  "the enumerator list is the whole enum"_test = [] {
    // The list above is hand-written, so it needs its own check: the last
    // enumerator's value must equal the list length, which fails the moment one
    // is added or removed without the list being updated.
    static_assert(static_cast<int>(ce::errc::missing_required_attribute) == 1);
    static_assert(static_cast<int>(ce::errc::invalid_argument) ==
                  static_cast<int>(all_codes.size()));
    expect(true);
  };
};

}  // namespace

int main() {}
