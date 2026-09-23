#include <boost/ut.hpp>

#include <cloudevents/v2/core.hpp>
#include <cloudevents/result.hpp>

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

// A decoder learns the attributes one field at a time, in wire order, and cannot
// know until the message is exhausted whether `id` arrived. The builder is the
// shape that accumulates them, and absence is the only thing `build` can refuse:
// every other way of being wrong was refused by an attribute's factory before the
// value reached the builder.

namespace {

using namespace std::string_view_literals;
using namespace ce::v2::literals;

/// True when `built` failed as missing_required_attribute, naming `where`.
[[nodiscard]] auto absent(const ce::v2::result<ce::v2::event>& built, std::string_view where) -> bool {
  return !built.has_value() && built.error().code == ce::v2::errc::missing_required_attribute &&
         built.error().where == where;
}

}  // namespace

const boost::ut::suite<"event-builder-requires-id-source-and-type"> event_builder = [] {
  using namespace boost::ut;

  "an empty builder reports id first"_test = [] {
    expect(absent(ce::v2::event::builder{}.build(), "id"));
  };

  // Each case supplies everything before the attribute it expects to be named,
  // so the report is the first one absent rather than any one absent.
  "the report names the first attribute that is absent"_test = [] {
    expect(absent(ce::v2::event::builder{.source = "/s"_source, .type = "t"_type}.build(), "id"));
    expect(absent(ce::v2::event::builder{.id = "1"_id, .type = "t"_type}.build(), "source"));
    expect(absent(ce::v2::event::builder{.id = "1"_id, .source = "/s"_source}.build(), "type"));
  };

  "with all three it builds the event they describe"_test = [] {
    auto built = ce::v2::event::builder{
        .id = "1"_id,
        .source = "/s"_source,
        .type = "t"_type,
        .rest = {.subject = "x"_subject},
    }.build();
    expect(built.has_value());
    if (!built) {
      return;
    }
    expect(*built == ce::v2::event{"1"_id, "/s"_source, "t"_type, {.subject = "x"_subject}});
  };

  // Rvalue-qualified, so a builder cannot be built twice or left half-consumed.
  "build consumes the builder"_test = [] {
    static_assert(std::is_invocable_v<decltype(&ce::v2::event::builder::build), ce::v2::event::builder&&>);
    static_assert(!std::is_invocable_v<decltype(&ce::v2::event::builder::build), ce::v2::event::builder&>);
    expect(true);
  };
};

int main() {}
