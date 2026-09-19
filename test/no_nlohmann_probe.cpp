/// \file
/// \brief Every public SDK header EXCEPT the nlohmann codec, compiled alone.
///
/// This translation unit is the evidence behind SWR-JSON-0008. If any header
/// included below starts to include nlohmann, or to name an nlohmann type, then
/// NLOHMANN_JSON_VERSION_MAJOR becomes defined here and the build stops at the
/// point the mistake was made rather than in CI.
///
/// Nothing here may include nlohmann, directly or through another test header.

#include "no_nlohmann_probe.hpp"

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/result.hpp>

#include "mini_codec.hpp"

#include <string>
#include <variant>

#ifdef NLOHMANN_JSON_VERSION_MAJOR
#error "an SDK header outside include/cloudevents/codec/nlohmann.hpp pulled in nlohmann"
#endif

namespace ce_no_nlohmann {
namespace {

/// Read back at run time so the test can assert the detector is not vacuous: the
/// same expression is true in a translation unit that DOES include nlohmann.
constexpr bool nlohmann_seen_here =
#ifdef NLOHMANN_JSON_VERSION_MAJOR
    true;
#else
    false;
#endif

}  // namespace

auto probe() -> report {
  using format = ce::json_format<ce::test::mini_codec>;
  static_assert(ce::json::json_codec<ce::test::mini_codec>);

  ce::event subject{.id = "1", .source = "/spec/test", .type = "com.example.thing"};
  subject.datacontenttype = "application/json";
  subject.data = ce::json_text{.raw = R"({"k":1})"};

  auto encoded = format::encode(subject);
  if (!encoded) {
    return report{
        .nlohmann_macro_defined = nlohmann_seen_here,
        .format_round_tripped = false,
        .encoded = {},
    };
  }

  auto decoded = format::decode(*encoded);
  const bool round_tripped = decoded.has_value() && decoded->id == subject.id &&
                             decoded->type == subject.type &&
                             std::holds_alternative<ce::json_text>(decoded->data);

  return report{
      .nlohmann_macro_defined = nlohmann_seen_here,
      .format_round_tripped = round_tripped,
      .encoded = std::move(*encoded),
  };
}

}  // namespace ce_no_nlohmann
