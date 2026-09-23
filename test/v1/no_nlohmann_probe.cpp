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

#include <cloudevents/v1/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/v1/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/v1/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/v1/format/typed_payload.hpp>
#include <cloudevents/result.hpp>

#include "mini_codec.hpp"

#include <cstdint>
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

struct parcel {
  std::string label;
  std::int32_t weight;
};

CE_DESCRIBE(parcel, label, weight);

}  // namespace

auto probe() -> report {
  using format = ce::v1::json_format<ce::v1::test::mini_codec>;
  static_assert(ce::v1::json::json_codec<ce::v1::test::mini_codec>);

  ce::v1::event subject{.id = "1", .source = "/spec/test", .type = "com.example.thing"};
  subject.datacontenttype = "application/json";
  subject.data = ce::v1::json_text{.raw = R"({"k":1})"};

  auto encoded = format::encode(subject);
  if (!encoded) {
    return report{
        .nlohmann_macro_defined = nlohmann_seen_here,
        .format_round_tripped = false,
        .encoded = {},
        .typed_payload_round_tripped = false,
        .typed_extension_round_tripped = false,
    };
  }

  auto decoded = format::decode(*encoded);
  const bool round_tripped = decoded.has_value() && decoded->id == subject.id &&
                             decoded->type == subject.type &&
                             std::holds_alternative<ce::v1::json_text>(decoded->data);

  // The typed payload accessors, over the same user-supplied codec.
  ce::v1::event typed{.id = "2", .source = "/spec/test", .type = "com.example.parcel"};
  const parcel sent{.label = "crate", .weight = 12};
  const bool stored = ce::v1::set_data<parcel, ce::v1::test::mini_codec>(typed, sent).has_value();
  auto read = ce::v1::data_as<parcel, ce::v1::test::mini_codec>(typed);
  const bool payload_round_tripped =
      stored && read.has_value() && read->label == sent.label && read->weight == sent.weight;

  // The typed extension layer, which is core and needs no codec at all.
  ce::v1::event tagged{.id = "3", .source = "/spec/test", .type = "com.example.traced"};
  const bool wrote_extension =
      tagged.set(ce::v1::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}}).has_value();
  auto tracing = tagged.get<ce::v1::ext::tracing>();
  const bool extension_round_tripped =
      wrote_extension && tracing.has_value() && tracing->traceparent == "00-a-b-01";

  return report{
      .nlohmann_macro_defined = nlohmann_seen_here,
      .format_round_tripped = round_tripped,
      .encoded = std::move(*encoded),
      .typed_payload_round_tripped = payload_round_tripped,
      .typed_extension_round_tripped = extension_round_tripped,
  };
}

}  // namespace ce_no_nlohmann
