#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/describe_macro.hpp>

#if CE_HAS_REFLECTION
#include <cloudevents/detail/describe_reflection.hpp>
#endif

// spec: SYS-DESC-0001
namespace ce::inline v1 {

// spec: SWR-DESC-0011
enum class describe_backend : std::uint8_t { macro, reflection };

template <std::size_t N>
struct name {
  std::array<char, N> value{};

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  consteval explicit(false) name(const char (&text)[N]) : value{std::to_array(text)} {}
};

template <std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
name(const char (&)[N]) -> name<N>;

struct skip {};

struct reflect {};

namespace detail {

template <class T>
concept macro_described = requires { ce_describe_fields(describe_tag<T>{}); };

#if CE_HAS_REFLECTION
template <class T>
concept reflection_described = !macro_described<T> && reflection_opted_in<T>();
#else
template <class T>
concept reflection_described = false;
#endif

// spec: SWR-DESC-0008
template <class T>
[[nodiscard]] constexpr auto descriptor_of() {
  if constexpr (macro_described<T>) {
    return ce_describe_fields(describe_tag<T>{});
  } else {
#if CE_HAS_REFLECTION
    return reflect_describe_fields<T>();
#else
    return std::tuple{};
#endif
  }
}

// spec: SWR-DESC-0009
template <class M>
struct supported_field_type : std::false_type {};

template <>
struct supported_field_type<bool> : std::true_type {};
template <>
struct supported_field_type<std::int32_t> : std::true_type {};
template <>
struct supported_field_type<std::int64_t> : std::true_type {};
template <>
struct supported_field_type<double> : std::true_type {};
template <>
struct supported_field_type<std::string> : std::true_type {};

template <class U>
struct supported_field_type<std::optional<U>> : supported_field_type<U> {};
template <class U>
struct supported_field_type<std::vector<U>> : supported_field_type<U> {};
template <class U>
struct supported_field_type<std::map<std::string, U>> : supported_field_type<U> {};

template <class M>
concept supported_field = supported_field_type<std::remove_cvref_t<M>>::value;

}  // namespace detail

// spec: SWR-DESC-0001
template <class T>
concept described =
    detail::macro_described<std::remove_cvref_t<T>> ||
    detail::reflection_described<std::remove_cvref_t<T>>;

template <described T>
inline constexpr describe_backend backend_of =
    detail::macro_described<std::remove_cvref_t<T>> ? describe_backend::macro
                                                    : describe_backend::reflection;

// spec: SWR-DESC-0003
template <described T>
inline constexpr std::size_t field_count =
    std::tuple_size_v<decltype(detail::descriptor_of<std::remove_cvref_t<T>>())>;

template <described T>
[[nodiscard]] constexpr auto field_names() -> std::array<std::string_view, field_count<T>> {
  return std::apply(
      [](auto... member) {
        return std::array<std::string_view, field_count<T>>{member.name...};
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

// spec: SWR-DESC-0002
template <described T, class F>
constexpr void for_each_field(T& object, const F& visit) {
  std::apply(
      [&object, &visit](auto... member) {
        (static_cast<void>(visit(member.name, object.*member.ptr)), ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

template <described T, class F>
constexpr void for_each_field(const T& object, const F& visit) {
  std::apply(
      [&object, &visit](auto... member) {
        (static_cast<void>(visit(member.name, object.*member.ptr)), ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

// spec: SWR-DESC-0010
template <described T>
[[nodiscard]] consteval auto members_supported() -> bool {
  bool supported = true;
  std::apply(
      [&supported](auto... member) {
        ((supported = supported &&
                      detail::supported_field<
                          std::remove_cvref_t<decltype(std::declval<T&>().*member.ptr)>>),
         ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
  return supported;
}

}  // namespace ce::inline v1
