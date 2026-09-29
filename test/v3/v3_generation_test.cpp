#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/attributes.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/v3/binding/http.hpp>
#include <cloudevents/v3/core.hpp>
#include <cloudevents/v3/format/json_format.hpp>

#include "mini_codec.hpp"

namespace {

using namespace std::string_view_literals;
using codec = ce::test::mini_codec;

/// v0.5.0's data_t: no payload, text, binary, JSON text and a JSON document.
constexpr std::size_t v050_payload_alternatives = 5;

/// The seven public helpers v0.5.0's json_format declared. ce::v4 drops them
/// (CR-0004 item 3); ce::v3 keeps them with their v0.5.0 signatures.
template<class Format>
concept declares_v050_attribute_helpers = requires(const Format::value& document,
                                                   std::string_view name,
                                                   std::optional<ce::v3::id>& slot,
                                                   ce::v3::event::options& options,
                                                   ce::v3::event::builder& builder) {
  { Format::required_text(document, name) } -> std::same_as<ce::v3::result<std::string_view>>;
  {
    Format::optional_text(document, name)
  } -> std::same_as<ce::v3::result<std::optional<std::string_view>>>;
  { Format::read_required(document, name, slot) } -> std::same_as<ce::v3::result<void>>;
  { Format::read_optional(document, name, slot) } -> std::same_as<ce::v3::result<void>>;
  { Format::read_time(document, options) } -> std::same_as<ce::v3::result<void>>;
  { Format::read_context_attributes(document, builder) } -> std::same_as<ce::v3::result<void>>;
  { Format::read_extensions(document, options) } -> std::same_as<ce::v3::result<void>>;
};

[[nodiscard]] auto v3_document(std::string_view text) -> ce::v3::json_document {
  auto parsed = codec::parse(text);
  return ce::v3::json_document::make<codec>(std::move(*parsed));
}

// spec: SWR-BUILD-0006
// spec: SWR-BUILD-0013
const boost::ut::suite<"v3-declarations-survive"> v3_declarations_survive = [] {
  using namespace boost::ut;

  // What a test can pin about the v0.6.0 break is that it left ce::v3 as v0.5.0
  // published it. Kept apart from build_test.cpp, which the clang-tidy gate
  // lints: including a v3 copy there would put the frozen generation under the
  // gate (D-TIDY-5).
  "the v0.6.0 event model did not replace the v3 one"_test = [] {
    static_assert(!std::is_same_v<ce::v3::event, ce::event>);
    static_assert(!std::is_same_v<ce::v3::event, ce::v4::event>);
    static_assert(!std::is_same_v<ce::v3::json_document, ce::json_document>);
    static_assert(std::variant_size_v<ce::v3::data_t> == v050_payload_alternatives);
    static_assert(
        std::is_same_v<std::variant_alternative_t<4, ce::v3::data_t>, ce::v3::json_document>);
    static_assert(!std::is_aggregate_v<ce::v3::event>);
    expect(true);
  };

  "v3 json_format keeps the helpers v0.5.0 declared"_test = [] {
    static_assert(declares_v050_attribute_helpers<ce::v3::json_format<codec>>);
    expect(true);
  };

  // The attribute types, the message and the decode options are shared rather
  // than copied, so a v3 event and a v4 event exchange them without a conversion.
  "a v3 event is built from the types v4 uses"_test = [] {
    static_assert(
        std::is_same_v<decltype(std::declval<const ce::v3::event&>().id()), const ce::v4::id&>);
    static_assert(std::is_same_v<ce::v3::event::extension_map, ce::v4::event::extension_map>);
    static_assert(std::is_same_v<ce::v3::json::decode_options, ce::v4::json::decode_options>);
    expect(true);
  };

  "the v3 format and bindings are reachable under ce::v3"_test = [] {
    using namespace ce::v3::literals;
    using format = ce::v3::json_format<codec>;
    auto built =
        ce::v3::event::builder{.id = "1"_id, .source = "/s"_source, .type = "t"_type}.build();
    expect(fatal(built.has_value()));
    static_assert(
        std::is_same_v<decltype(format::decode(std::string{})), ce::v3::result<ce::v3::event>>);
    auto text = format::encode(*built);
    expect(fatal(text.has_value()));
    auto back = format::decode(*text);
    expect(back.has_value() && *back == *built);
    auto request = ce::v3::http::to_message<codec>(*built, ce::v3::content_mode::binary_mode);
    expect(fatal(request.has_value()));
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(*request)>, ce::v4::message>);
    auto received = ce::v3::http::from_message<codec>(*request);
    expect(received.has_value() && *received == *built);
  };
};

// spec: SWR-CORE-0036
const boost::ut::suite<"v3-document-move-copies"> v3_document_move_copies = [] {
  using namespace boost::ut;

  "moving a v3 document leaves the source holding its document"_test = [] {
    auto source = v3_document(R"({"a":[1,2]})"sv);
    const auto* const dom = source.get<codec>();
    // v3 declares no move constructor, so this move is a copy: that is the pin.
    const auto taken = std::move(source);  // NOLINT(performance-move-const-arg)
    expect(taken.get<codec>() == dom);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(source.dump() == taken.dump()) << "moving a v3 document copies it";
    expect(source.get<codec>() == dom) << "the source still yields its DOM";
    expect(source.built_by<codec>());
    expect(source == taken);
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  };

  "move-assigning a v3 document leaves the source holding its document"_test = [] {
    auto target = v3_document(R"([true])"sv);
    auto source = v3_document(R"({"b":"c"})"sv);
    const auto* const dom = source.get<codec>();
    target = std::move(source);  // NOLINT(performance-move-const-arg)
    expect(target.get<codec>() == dom);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(source.dump() == target.dump());
    expect(source.get<codec>() == dom);
    expect(source == target);
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  };

  "an event moved from keeps its v3 payload"_test = [] {
    using namespace ce::v3::literals;
    ce::v3::event source{
        "1"_id, "/s"_source, "t"_type, {.data = ce::v3::data_t{v3_document(R"({"k":1})"sv)}}};
    const auto taken = std::move(source);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    const auto* held = std::get_if<ce::v3::json_document>(&source.data());
    expect(fatal(held != nullptr));
    expect(held->dump() == std::get<ce::v3::json_document>(taken.data()).dump());
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  };

  // The contrast that makes the v3 behaviour a pin rather than an accident: the
  // same move on the v4 document leaves the SWR-CORE-0035 moved-from state.
  "the v4 document moves where the v3 one copies"_test = [] {
    auto parsed = codec::parse(R"({"a":1})"sv);
    auto source = ce::v4::json_document::make<codec>(std::move(*parsed));
    const auto taken = std::move(source);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(source.dump() == "null"sv);
    expect(source.get<codec>() == nullptr);
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(taken.dump() != "null"sv);
  };
};

}  // namespace

int main() {}
