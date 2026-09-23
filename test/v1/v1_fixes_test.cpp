#include <boost/ut.hpp>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/v1/binding/http.hpp>
#include <cloudevents/v1/core.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The defect fixes ce::v1 received without changing a declaration (CR-0002).
// The v0.3.0 suites never asserted the defective behaviour, so they cannot show
// the fixes landed; this suite does.

namespace {

using codec = ce::v1::codec::nlohmann_codec;

// ce::v1::headers has no list constructor: v0.3.0 published none, and ce::v1
// keeps its declarations, so a v1 message is filled the way v0.3.0 callers did.
[[nodiscard]] auto binary_request(const std::vector<std::pair<std::string, std::string>>& fields)
    -> ce::v1::message {
  ce::v1::message request;
  for (const auto& [name, value] : fields) {
    request.header_fields.add(name, value);
  }
  return request;
}

// spec: SWR-BUILD-0011
const boost::ut::suite<"v1-defect-fixes"> v1_defect_fixes = [] {
  using namespace boost::ut;

  "a digit count above nine renders nine digits instead of dividing by zero"_test = [] {
    const ce::v1::timestamp stamp{
        .utc = std::chrono::sys_time<std::chrono::nanoseconds>{std::chrono::nanoseconds{123456789}},
        .fractional_digits = std::uint8_t{12},
    };
    expect(ce::v1::to_string(stamp) == "1970-01-01T00:00:00.123456789Z");
  };

  "base64 refuses padding the final quantum does not need"_test = [] {
    const auto decoded = ce::v1::base64_decode("QQ======");
    expect(!decoded);
    expect(!decoded && decoded.error().code == ce::v1::errc::invalid_base64);
  };

  "a repeated attribute field is refused rather than resolved"_test = [] {
    const auto received = ce::v1::http::from_message<codec>(binary_request({
        {"ce-specversion", "1.0"},
        {"ce-id", "first"},
        {"ce-id", "second"},
        {"ce-source", "/s"},
        {"ce-type", "t"},
    }));
    expect(!received);
    expect(!received && received.error().code == ce::v1::errc::invalid_argument);
    expect(!received && received.error().where == "ce-id");
  };

  "a prefixed datacontenttype names the problem where HTTP has a content-type field"_test = [] {
    const auto received = ce::v1::http::from_message<codec>(binary_request({
        {"ce-specversion", "1.0"},
        {"ce-id", "1"},
        {"ce-source", "/s"},
        {"ce-type", "t"},
        {"ce-datacontenttype", "text/plain"},
    }));
    expect(!received);
    expect(!received && received.error().code == ce::v1::errc::invalid_argument);
    expect(!received && received.error().where == "datacontenttype");
  };
};

}  // namespace

int main() {}
