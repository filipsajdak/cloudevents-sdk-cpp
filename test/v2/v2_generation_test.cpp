#include <boost/ut.hpp>

#include <cloudevents/attributes.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/v2/format/json_format.hpp>

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "mini_codec.hpp"

namespace {

// spec: SWR-BUILD-0006
// spec: SWR-BUILD-0012
const boost::ut::suite<"v2-declarations-survive"> v2_declarations_survive = [] {
  using namespace boost::ut;

  // What a test can pin about the v0.5.0 break is that it left ce::v2 as v0.4.0
  // published it. Kept apart from build_test.cpp, which the clang-tidy gate
  // lints: including a v2 copy there would put the frozen generation under the
  // gate (D-TIDY-5).
  "the v0.5.0 event model did not replace the v2 one"_test = [] {
    static_assert(!std::is_same_v<ce::v2::event, ce::event>);
    static_assert(!std::is_same_v<ce::v2::event, ce::v3::event>);
    static_assert(std::variant_size_v<ce::v2::data_t> == 4);
    static_assert(std::is_same_v<std::variant_alternative_t<3, ce::v2::data_t>, ce::v2::json_text>);
    static_assert(!std::is_aggregate_v<ce::v2::event>);
    expect(true);
  };

  // The attribute types are shared rather than copied, so a v2 event and a v3
  // event exchange them without a conversion.
  "a v2 event is built from the attribute types v3 uses"_test = [] {
    static_assert(
        std::is_same_v<decltype(std::declval<const ce::v2::event&>().id()), const ce::v3::id&>);
    static_assert(std::is_same_v<ce::v2::event::extension_map, ce::v3::event::extension_map>);
    expect(true);
  };

  "the v2 format and bindings are reachable under ce::v2"_test = [] {
    using namespace ce::v2::literals;
    using codec = ce::test::mini_codec;
    using format = ce::v2::json_format<codec>;
    auto built =
        ce::v2::event::builder{.id = "1"_id, .source = "/s"_source, .type = "t"_type}.build();
    expect(fatal(built.has_value()));
    static_assert(
        std::is_same_v<decltype(format::decode(std::string{})), ce::v2::result<ce::v2::event>>);
    auto text = format::encode(*built);
    expect(fatal(text.has_value()));
    auto back = format::decode(*text);
    expect(back.has_value() && *back == *built);
    auto request = ce::v2::http::to_message<codec>(*built, ce::v2::content_mode::binary_mode);
    expect(fatal(request.has_value()));
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(*request)>, ce::v3::message>);
    auto received = ce::v2::http::from_message<codec>(*request);
    expect(received.has_value() && *received == *built);
  };
};

}  // namespace

int main() {}
