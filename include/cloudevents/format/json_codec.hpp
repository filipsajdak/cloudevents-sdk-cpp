#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <cloudevents/result.hpp>

namespace ce::v1::json {

// spec: SWR-JSON-0034
enum class kind : std::uint8_t { null, boolean, integer, floating, string, array, object };

namespace detail {
template <class V>
struct member_probe {
  void operator()([[maybe_unused]] std::string_view name,
                  [[maybe_unused]] const V& value) const {}
};
template <class V>
struct element_probe {
  void operator()([[maybe_unused]] const V& value) const {}
};
}  // namespace detail

// spec: SWR-JSON-0001
template <class C>
concept json_codec = requires(C::value value, const C::value& const_value,
                              std::string_view text, std::int64_t integer, double number,
                              bool boolean) {
  typename C::value;
  requires std::movable<typename C::value>;

  // spec: SWR-JSON-0002
  { C::parse(text) } -> std::same_as<result<typename C::value>>;
  { C::dump(const_value) } -> std::same_as<std::string>;

  // spec: SWR-JSON-0003
  { C::make_null() } -> std::same_as<typename C::value>;
  { C::make_bool(boolean) } -> std::same_as<typename C::value>;
  { C::make_int(integer) } -> std::same_as<typename C::value>;
  { C::make_double(number) } -> std::same_as<typename C::value>;
  { C::make_string(text) } -> std::same_as<typename C::value>;
  { C::make_array() } -> std::same_as<typename C::value>;
  { C::make_object() } -> std::same_as<typename C::value>;

  // spec: SWR-JSON-0004
  { C::set(value,text, typename C::value{}) } -> std::same_as<void>;
  { C::push(value, typename C::value{}) } -> std::same_as<void>;

  // spec: SWR-JSON-0005
  { C::find(const_value, text) } -> std::same_as<const typename C::value*>;
  { C::size_of(const_value) } -> std::same_as<std::size_t>;

  // spec: SWR-JSON-0006
  { C::kind_of(const_value) } -> std::same_as<kind>;
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

inline constexpr std::string_view content_type = "application/cloudevents+json";

inline constexpr std::string_view batch_content_type = "application/cloudevents-batch+json";

}  // namespace ce::v1::json

namespace ce::v2 {
namespace json = ce::v1::json;
}  // namespace ce::v2

// spec: SWR-BUILD-0005
namespace ce::inline v3::json {
using ce::v1::json::batch_content_type;
using ce::v1::json::content_type;
using ce::v1::json::kind;

// spec: SWR-JSON-0039
template <class C>
concept json_codec = ce::v1::json::json_codec<C> && requires(const C::value& value,
                                                            C::value& object,
                                                            std::string_view key) {
  { C::equal(value, value) } -> std::same_as<bool>;
  { C::copy(value) } -> std::same_as<typename C::value>;
  { C::extract(object, key) } -> std::same_as<typename C::value>;
  { C::identity } -> std::convertible_to<std::string_view>;
  typename std::integral_constant<std::size_t, std::string_view{C::identity}.size()>;
  requires(!std::string_view{C::identity}.empty());
};
}  // namespace ce::inline v3::json
