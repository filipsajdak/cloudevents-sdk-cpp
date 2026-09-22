#pragma once

/// \file
/// \brief One field-enumeration interface over user structs, served by either
/// backend. Consumers never branch on which.

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

namespace ce::inline v1 {

/// \brief Which backend described a type. Queryable so a test can pin it.
enum class describe_backend : std::uint8_t { macro, reflection };

/// \brief Annotation giving a member a wire name, for the reflection backend.
template <std::size_t N>
struct name {
  // A std::string_view is not a structural type, so it cannot be an annotation.
  // std::array is, and it copies whole rather than element by element.
  std::array<char, N> value{};

  // The parameter stays a reference to a C array: that is the type of a string
  // literal, and the only form from which N can be deduced.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  consteval explicit(false) name(const char (&text)[N]) : value{std::to_array(text)} {}
};

template <std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
name(const char (&)[N]) -> name<N>;

/// \brief Annotation excluding a member from the description.
struct skip {};

/// \brief Annotation opting a type into the reflection backend.
///
/// Reflection never adopts a type on its own. Without this, enabling
/// `-freflection` would make `described<T>` newly true for every aggregate,
/// silently changing serialization for types nobody described (ADR-0003).
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

/// The one backend-varying function. The macro is tested first, so a type
/// carrying CE_DESCRIBE keeps its description when reflection is switched on.
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

/// \brief True when a type carries a field description.
template <class T>
concept described =
    detail::macro_described<std::remove_cvref_t<T>> ||
    detail::reflection_described<std::remove_cvref_t<T>>;

/// \brief Which backend describes `T`.
template <described T>
inline constexpr describe_backend backend_of =
    detail::macro_described<std::remove_cvref_t<T>> ? describe_backend::macro
                                                    : describe_backend::reflection;

/// \brief How many members `T` describes.
template <described T>
inline constexpr std::size_t field_count =
    std::tuple_size_v<decltype(detail::descriptor_of<std::remove_cvref_t<T>>())>;

/// \brief The wire names of `T`'s members, in declaration order.
template <described T>
[[nodiscard]] constexpr auto field_names() -> std::array<std::string_view, field_count<T>> {
  return std::apply(
      [](auto... member) {
        return std::array<std::string_view, field_count<T>>{member.name...};
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

/// \brief Call `visit(wire_name, member)` for each member, in declaration order.
template <described T, class F>
constexpr void for_each_field(T& object, F&& visit) {
  std::apply(
      [&object, &visit](auto... member) {
        (static_cast<void>(visit(member.name, object.*(member.ptr))), ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

/// \brief Call `visit(wire_name, member)` for each member of a const object.
template <described T, class F>
constexpr void for_each_field(const T& object, F&& visit) {
  std::apply(
      [&object, &visit](auto... member) {
        (static_cast<void>(visit(member.name, object.*(member.ptr))), ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
}

/// \brief True when every described member of `T` has a mappable type.
///
/// Reported through a `static_assert` at the point of use, so the diagnostic
/// names the offending struct rather than a template deep in the format layer.
template <described T>
[[nodiscard]] consteval auto members_supported() -> bool {
  bool supported = true;
  std::apply(
      [&supported](auto... member) {
        ((supported = supported &&
                      detail::supported_field<
                          std::remove_cvref_t<decltype(std::declval<T&>().*(member.ptr))>>),
         ...);
      },
      detail::descriptor_of<std::remove_cvref_t<T>>());
  return supported;
}

}  // namespace ce::inline v1
