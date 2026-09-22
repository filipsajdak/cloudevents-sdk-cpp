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
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
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
  using format = ce::json_format<ce::test::mini_codec>;
  using namespace ce::literals;
  static_assert(ce::json::json_codec<ce::test::mini_codec>);

  const ce::event subject{"1"_id, "/spec/test"_source, "com.example.thing"_type,
                          {.datacontenttype = "application/json"_mediatype,
                           .data = ce::json_text{.raw = R"({"k":1})"}}};

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
  const bool round_tripped = decoded.has_value() && decoded->id() == subject.id() &&
                             decoded->type() == subject.type() &&
                             std::holds_alternative<ce::json_text>(decoded->data());

  // The typed payload accessors, over the same user-supplied codec.
  ce::event typed{"2"_id, "/spec/test"_source, "com.example.parcel"_type};
  const parcel sent{.label = "crate", .weight = 12};
  // set_data cannot fail, so there is no longer a result to check: writing the
  // payload and reading it back is the whole round trip.
  ce::set_data<parcel, ce::test::mini_codec>(typed, sent);
  auto read = ce::data_as<parcel, ce::test::mini_codec>(typed);
  const bool payload_round_tripped =
      read.has_value() && read->label == sent.label && read->weight == sent.weight;

  // The typed extension layer, which is core and needs no codec at all.
  ce::event tagged{"3"_id, "/spec/test"_source, "com.example.traced"_type};
  const bool wrote_extension =
      tagged.set(ce::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}}).has_value();
  auto tracing = tagged.get<ce::ext::tracing>();
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
