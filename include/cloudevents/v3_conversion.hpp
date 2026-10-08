#pragma once

/// \file
/// \brief Conversions between `ce::v3` and `ce::v4` events (CR-0004, ADR-0012).
///
/// Opt-in: no other SDK header includes this one, and the optional module does
/// not export it, so a translation unit that uses only `ce::v4` never compiles
/// the `ce::v3` event model. Including it brings both.
///
/// An event converts by copying or moving its parts. The context attributes, the
/// extensions and every payload alternative except `json_document` are one type
/// in both generations. A `json_document` converts by copying or moving the
/// pointer to the document model both generations share, so no DOM is copied,
/// serialised or parsed.

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include <cloudevents/core.hpp>
#include <cloudevents/detail/json_document_model.hpp>
#include <cloudevents/v3/core.hpp>

namespace ce::v3::detail {

/// What a conversion needs to know about the generation it produces.
struct v3_target {
  using event = ce::v3::event;
  using data = ce::v3::data_t;
  using document = ce::v3::json_document;
};
struct v4_target {
  using event = ce::v4::event;
  using data = ce::v4::data_t;
  using document = ce::v4::json_document;
};

template<class Alternative>
inline constexpr bool is_generation_document = std::is_same_v<Alternative, ce::v3::json_document> ||
                                               std::is_same_v<Alternative, ce::v4::json_document>;

/// The friend both generations' documents and events name, so the conversions
/// reach their private parts without either generation widening its public API.
struct generation_access {
  /// Each part of a source is copied when `Move` is false and moved when it is
  /// true. The source is always a named object here, so one function serves both
  /// the `const&` and the `&&` entry point without forwarding.
  template<bool Move, class Member>
  [[nodiscard]] static constexpr auto part(Member& member) noexcept
      -> std::conditional_t<Move, Member&&, const Member&> {
    return static_cast<std::conditional_t<Move, Member&&, const Member&>>(member);
  }

  /// A document in the other generation, over the same model.
  /// Moving gives the model up, so a v4 source is left in the moved-from state
  /// (SWR-CORE-0035). A v3 source keeps its document, because moving a v3
  /// document copies it (SWR-CORE-0036).
  template<class Target, bool Move, class Source>
  [[nodiscard]] static auto convert_document(Source& original) -> Target::document {
    if constexpr (Move) {
      std::remove_cvref_t<Source> taken{std::move(original)};
      return typename Target::document{std::move(taken.model_)};
    } else {
      return typename Target::document{original.model_};
    }
  }

  /// A payload in the other generation: a document by its model, anything else
  /// as the one type both generations share.
  template<class Target, bool Move, class Data>
  [[nodiscard]] static auto convert_data(Data& original) -> Target::data {
    return std::visit(
        [](auto& held) -> Target::data {
          using alternative = std::remove_cvref_t<decltype(held)>;
          if constexpr (is_generation_document<alternative>) {
            return typename Target::data{
                std::in_place_type<typename Target::document>,
                convert_document<Target, Move>(held),
            };
          } else {
            return typename Target::data{std::in_place_type<alternative>, part<Move>(held)};
          }
        },
        original);
  }

  template<class Target, bool Move, class Source>
  [[nodiscard]] static auto convert_event(Source& original) -> Target::event {
    return typename Target::event{
        part<Move>(original.id_),
        part<Move>(original.source_),
        part<Move>(original.type_),
        typename Target::event::options{
            .datacontenttype = part<Move>(original.rest_.datacontenttype),
            .dataschema = part<Move>(original.rest_.dataschema),
            .subject = part<Move>(original.rest_.subject),
            .time = part<Move>(original.rest_.time),
            .extensions = part<Move>(original.rest_.extensions),
            .data = convert_data<Target, Move>(original.rest_.data),
        },
    };
  }
};

}  // namespace ce::v3::detail

namespace ce::inline v4 {

// spec: SWR-CORE-0037
// spec: SWR-CORE-0039
/// The `ce::v4` event equal to `original`. It cannot fail: every v3 event is a valid
/// v4 event. A document payload shares the original's model, and the original is
/// unchanged.
[[nodiscard]] inline auto from_v3(const ce::v3::event& original) -> event {
  return ce::v3::detail::generation_access::convert_event<ce::v3::detail::v4_target, false>(
      original);
}

/// As above, moving the original's parts. A v3 document payload is shared rather
/// than moved, because `ce::v3` copies a document on move.
// The parts are moved one by one inside convert_event, through the named original.
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
[[nodiscard]] inline auto from_v3(ce::v3::event&& original) -> event {
  return ce::v3::detail::generation_access::convert_event<ce::v3::detail::v4_target, true>(
      original);
}

// spec: SWR-CORE-0038
// spec: SWR-CORE-0039
// spec: SWR-CORE-0040
/// The `ce::v3` event equal to `original`. It cannot fail. A document payload shares
/// the original's model; a moved-from document becomes a v3 document sharing the
/// moved-from model, which dumps `null`, yields no DOM and equals only another
/// moved-from document.
[[nodiscard]] inline auto to_v3(const event& original) -> ce::v3::event {
  return ce::v3::detail::generation_access::convert_event<ce::v3::detail::v3_target, false>(
      original);
}

/// As above, moving the original's parts; a document payload leaves the original in
/// the moved-from state of `SWR-CORE-0035`.
// The parts are moved one by one inside convert_event, through the named original.
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
[[nodiscard]] inline auto to_v3(event&& original) -> ce::v3::event {
  return ce::v3::detail::generation_access::convert_event<ce::v3::detail::v3_target, true>(
      original);
}

}  // namespace ce::inline v4
