#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>

#if defined(_MSC_VER) && !defined(__clang__) && defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL
#  error "cloudevents: CE_DESCRIBE needs the conforming preprocessor; add /Zc:preprocessor. \
/permissive- does not imply it, and the traditional preprocessor miscounts the member list."
#endif

namespace ce::v1::detail {

template <class T>
struct describe_tag {};

template <class T, class M>
struct field {
  std::string_view name;
  M T::* ptr;
};

template <class T, class M>
field(std::string_view, M T::*) -> field<T, M>;

}  // namespace ce::v1::detail

#define CE_DETAIL_CAT(a_, b_) CE_DETAIL_CAT_IMPL(a_, b_)
#define CE_DETAIL_CAT_IMPL(a_, b_) a_##b_
#define CE_DETAIL_SECOND(a_, b_, ...) b_
#define CE_DETAIL_UNPAREN(...) __VA_ARGS__
#define CE_DETAIL_PROBE(...) ~, 1,
#define CE_DETAIL_IS_PAREN(x_) CE_DETAIL_IS_PAREN_IMPL(CE_DETAIL_PROBE x_)
#define CE_DETAIL_IS_PAREN_IMPL(...) CE_DETAIL_SECOND(__VA_ARGS__, 0, ~)

// spec: SWR-DESC-0005
#define CE_FIELD(member_, wire_) (member_, wire_)

#define CE_DETAIL_ENTRY(T_, e_) CE_DETAIL_CAT(CE_DETAIL_ENTRY_, CE_DETAIL_IS_PAREN(e_))(T_, e_)
#define CE_DETAIL_ENTRY_0(T_, m_) ::ce::v1::detail::field{::std::string_view{#m_}, &T_::m_}
#define CE_DETAIL_ENTRY_1(T_, e_) CE_DETAIL_ENTRY_1A(T_, CE_DETAIL_UNPAREN e_)
#define CE_DETAIL_ENTRY_1A(...) CE_DETAIL_ENTRY_1B(__VA_ARGS__)
#define CE_DETAIL_ENTRY_1B(T_, m_, w_) ::ce::v1::detail::field{::std::string_view{w_}, &T_::m_}

#define CE_DETAIL_NARG(...) CE_DETAIL_NARG_IMPL(__VA_ARGS__, 32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define CE_DETAIL_NARG_IMPL(...) CE_DETAIL_ARGN(__VA_ARGS__)
#define CE_DETAIL_ARGN(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,N_,...) N_

#define CE_DETAIL_FE_1(T_, e_) CE_DETAIL_ENTRY(T_, e_)
#define CE_DETAIL_FE_2(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_1(T_, __VA_ARGS__)
#define CE_DETAIL_FE_3(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_2(T_, __VA_ARGS__)
#define CE_DETAIL_FE_4(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_3(T_, __VA_ARGS__)
#define CE_DETAIL_FE_5(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_4(T_, __VA_ARGS__)
#define CE_DETAIL_FE_6(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_5(T_, __VA_ARGS__)
#define CE_DETAIL_FE_7(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_6(T_, __VA_ARGS__)
#define CE_DETAIL_FE_8(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_7(T_, __VA_ARGS__)
#define CE_DETAIL_FE_9(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_8(T_, __VA_ARGS__)
#define CE_DETAIL_FE_10(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_9(T_, __VA_ARGS__)
#define CE_DETAIL_FE_11(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_10(T_, __VA_ARGS__)
#define CE_DETAIL_FE_12(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_11(T_, __VA_ARGS__)
#define CE_DETAIL_FE_13(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_12(T_, __VA_ARGS__)
#define CE_DETAIL_FE_14(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_13(T_, __VA_ARGS__)
#define CE_DETAIL_FE_15(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_14(T_, __VA_ARGS__)
#define CE_DETAIL_FE_16(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_15(T_, __VA_ARGS__)
#define CE_DETAIL_FE_17(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_16(T_, __VA_ARGS__)
#define CE_DETAIL_FE_18(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_17(T_, __VA_ARGS__)
#define CE_DETAIL_FE_19(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_18(T_, __VA_ARGS__)
#define CE_DETAIL_FE_20(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_19(T_, __VA_ARGS__)
#define CE_DETAIL_FE_21(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_20(T_, __VA_ARGS__)
#define CE_DETAIL_FE_22(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_21(T_, __VA_ARGS__)
#define CE_DETAIL_FE_23(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_22(T_, __VA_ARGS__)
#define CE_DETAIL_FE_24(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_23(T_, __VA_ARGS__)
#define CE_DETAIL_FE_25(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_24(T_, __VA_ARGS__)
#define CE_DETAIL_FE_26(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_25(T_, __VA_ARGS__)
#define CE_DETAIL_FE_27(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_26(T_, __VA_ARGS__)
#define CE_DETAIL_FE_28(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_27(T_, __VA_ARGS__)
#define CE_DETAIL_FE_29(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_28(T_, __VA_ARGS__)
#define CE_DETAIL_FE_30(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_29(T_, __VA_ARGS__)
#define CE_DETAIL_FE_31(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_30(T_, __VA_ARGS__)
#define CE_DETAIL_FE_32(T_, e_, ...) CE_DETAIL_ENTRY(T_, e_), CE_DETAIL_FE_31(T_, __VA_ARGS__)
#define CE_DETAIL_FE(T_, ...) CE_DETAIL_CAT(CE_DETAIL_FE_, CE_DETAIL_NARG(__VA_ARGS__))(T_, __VA_ARGS__)

// spec: SWR-DESC-0004
#define CE_DESCRIBE(Type_, ...)                                                     \
  [[maybe_unused]] inline constexpr auto ce_describe_fields(                          \
      [[maybe_unused]] ::ce::v1::detail::describe_tag<Type_> ce_tag_) noexcept {        \
    return ::std::tuple{CE_DETAIL_FE(Type_, __VA_ARGS__)};                             \
  }                                                                                    \
  static_assert(true, "CE_DESCRIBE requires a trailing semicolon")
