#pragma once

/// \file
/// \brief What the JSON layer needs from a JSON library: a DOM, not an event.
///
/// Abstracting the event instead would duplicate every CloudEvents format rule in
/// every codec, which is the logic that has to be right once (ADR-0004).

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <cloudevents/result.hpp>

namespace ce::inline v1::json {

/// \brief The JSON types the CloudEvents format distinguishes.
enum class kind : std::uint8_t { null, boolean, integer, floating, string, array, object };

namespace detail {
template <class V>
struct member_probe {
  void operator()(std::string_view /*name*/, const V& /*value*/) const {}
};
template <class V>
struct element_probe {
  void operator()(const V& /*value*/) const {}
};
}  // namespace detail

/// \brief A JSON DOM the format layer can drive.
///
/// `find` returns a pointer because the format has to tell *absent* from
/// *present-and-null*: `data` absent is not `data: null`, and the
/// `data`/`data_base64` exclusion is decided on presence.
///
/// `as_int` is separate from `as_double` because the CloudEvents `Integer` type is
/// 32-bit signed, and `1.0` must not satisfy it.
///
/// Three rules the in-tree codecs disagreed about until they were written down:
///
/// `kind_of` reports `kind::integer` when the JSON text carried no fractional
/// part and no exponent, whatever the magnitude. A value too large for
/// `std::int64_t` is still an integer; refusing it is `as_int`'s job, and
/// reporting it as `floating` would make the format layer diagnose it as a
/// fractional extension value, which is a different and wrong complaint.
///
/// `as_int` returns `errc::out_of_range` for an integer it cannot represent. Not
/// `type_mismatch`: the value is an integer, and one that merely exceeds the
/// 32-bit `Integer` type already reports `out_of_range`. A codec may instead
/// refuse such a document at `parse`, which is equally conformant - what is
/// forbidden is returning a value that is not the one on the wire.
///
/// `size_of` is the element count of an array or the member count of an object.
/// It is not defined for any other kind, and `json_format` never calls it on one.
/// The shipped nlohmann codec returns 1 for a scalar where others return 0; do
/// not depend on either.
template <class C>
concept json_codec = requires(C::value value, const C::value& const_value,
                              std::string_view text, std::int64_t integer, double number,
                              bool boolean) {
  typename C::value;
  requires std::movable<typename C::value>;

  { C::parse(text) } -> std::same_as<result<typename C::value>>;
  { C::dump(const_value) } -> std::same_as<std::string>;

  { C::make_null() } -> std::same_as<typename C::value>;
  { C::make_bool(boolean) } -> std::same_as<typename C::value>;
  { C::make_int(integer) } -> std::same_as<typename C::value>;
  { C::make_double(number) } -> std::same_as<typename C::value>;
  { C::make_string(text) } -> std::same_as<typename C::value>;
  { C::make_array() } -> std::same_as<typename C::value>;
  { C::make_object() } -> std::same_as<typename C::value>;

  { C::set(value, text, typename C::value{}) } -> std::same_as<void>;
  { C::push(value, typename C::value{}) } -> std::same_as<void>;

  { C::kind_of(const_value) } -> std::same_as<kind>;
  { C::find(const_value, text) } -> std::same_as<const typename C::value*>;
  { C::size_of(const_value) } -> std::same_as<std::size_t>;

  { C::as_bool(const_value) } -> std::same_as<result<bool>>;
  { C::as_int(const_value) } -> std::same_as<result<std::int64_t>>;
  { C::as_double(const_value) } -> std::same_as<result<double>>;
  { C::as_string(const_value) } -> std::same_as<result<std::string_view>>;

  requires requires(detail::member_probe<typename C::value> visit_member) {
    { C::for_each_member(const_value, visit_member) } -> std::same_as<void>;
  };
  requires requires(detail::element_probe<typename C::value> visit_element) {
    { C::for_each_element(const_value, visit_element) } -> std::same_as<void>;
  };
};

/// \brief The media type of a single JSON-formatted event.
inline constexpr std::string_view content_type = "application/cloudevents+json";

/// \brief The media type of a JSON-formatted batch.
inline constexpr std::string_view batch_content_type = "application/cloudevents-batch+json";

}  // namespace ce::inline v1::json
