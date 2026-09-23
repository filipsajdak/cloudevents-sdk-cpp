#pragma once

/// \file
/// \brief The C++26 static reflection backend.
///
/// Produces the same `field{name, ptr}` tuple the macro backend does, so nothing
/// downstream can tell which one described a type.

#include <cstddef>
#include <string_view>
#include <tuple>
#include <vector>

#include <meta>

#include <cloudevents/detail/describe_macro.hpp>

namespace ce::inline v1 {
template <std::size_t N>
struct name;
struct skip;
struct reflect;
}  // namespace ce::inline v1

namespace ce::inline v1::detail {

/// \brief True when `T` carries `[[=ce::reflect]]`.
///
/// Reflection describes only types that ask for it. Adopting every aggregate
/// would make `described<T>` depend on a compiler flag (ADR-0003).
template <class T>
consteval auto reflection_opted_in() -> bool {
  return !std::meta::annotations_of_with_type(^^T, ^^reflect).empty();
}

/// \brief The public, non-skipped members of `T`, in declaration order.
consteval auto reflected_members(std::meta::info type) -> std::vector<std::meta::info> {
  std::vector<std::meta::info> kept;
  for (const std::meta::info member :
       std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged())) {
    if (!std::meta::is_public(member)) {
      continue;
    }
    if (!std::meta::annotations_of_with_type(member, ^^skip).empty()) {
      continue;
    }
    kept.push_back(member);
  }
  return kept;
}

/// \brief A member's wire name: its `[[=ce::name("x")]]` if it has one, else its
/// identifier.
///
/// The result is interned with `define_static_string`, because extracting an
/// annotation yields a prvalue whose array a `string_view` would outlive.
template <std::meta::info M>
consteval auto reflected_name() -> std::string_view {
  template for (constexpr std::meta::info annotation :
                std::define_static_array(std::meta::annotations_of(M))) {
    constexpr std::meta::info annotation_type =
        std::meta::dealias(std::meta::remove_const(std::meta::type_of(annotation)));
    if constexpr (std::meta::has_template_arguments(annotation_type) &&
                  std::meta::template_of(annotation_type) == ^^name) {
      constexpr auto renamed = std::meta::extract<typename[:annotation_type:]>(annotation);
      return std::define_static_string(
          std::string_view{renamed.value.data(), renamed.value.size() - 1});
    }
  }
  return std::define_static_string(std::meta::identifier_of(M));
}

template <class T>
constexpr auto reflected_members_of = std::define_static_array(reflected_members(^^T));

/// \brief Build the descriptor tuple for `T`.
template <class T>
consteval auto reflect_describe_fields() {
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple{
        field{reflected_name<reflected_members_of<T>[I]>(), &[:reflected_members_of<T>[I]:]}...};
  }(std::make_index_sequence<reflected_members_of<T>.size()>{});
}

}  // namespace ce::inline v1::detail
