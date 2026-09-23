#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <cloudevents/describe.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::v2 {

namespace detail {

template<class F>
inline constexpr bool is_json_optional = false;
template<class U>
inline constexpr bool is_json_optional<std::optional<U>> = true;

template<class F>
inline constexpr bool is_json_vector = false;
template<class U>
inline constexpr bool is_json_vector<std::vector<U>> = true;

template<class F>
inline constexpr bool is_json_map = false;
template<class U>
inline constexpr bool is_json_map<std::map<std::string, U>> = true;

template<json::json_codec Codec, class F>
[[nodiscard]] auto field_to_json(const F& field) -> Codec::value {
  if constexpr (is_json_optional<F>) {
    return field ? field_to_json<Codec>(*field) : Codec::make_null();
  } else if constexpr (is_json_vector<F>) {
    auto array = Codec::make_array();
    for (const auto& element : field) {
      Codec::push(array, field_to_json<Codec>(element));
    }
    return array;
  } else if constexpr (is_json_map<F>) {
    auto object = Codec::make_object();
    for (const auto& [key, element] : field) {
      Codec::set(object, key, field_to_json<Codec>(element));
    }
    return object;
  } else if constexpr (std::is_same_v<F, bool>) {
    return Codec::make_bool(field);
  } else if constexpr (std::is_same_v<F, std::string>) {
    return Codec::make_string(field);
  } else if constexpr (std::is_same_v<F, double>) {
    return Codec::make_double(field);
  } else {
    return Codec::make_int(static_cast<std::int64_t>(field));
  }
}

template<json::json_codec Codec, class F>
[[nodiscard]] auto field_from_json(const typename Codec::value& held,
                                   std::string_view where,
                                   F& out) -> result<void>;

template<json::json_codec Codec, class F>
[[nodiscard]] auto array_from_json(const typename Codec::value& held,
                                   std::string_view where,
                                   F& out) -> result<void> {
  if (Codec::kind_of(held) != json::kind::array) {
    return fail(errc::type_mismatch, "expected a JSON array", std::string{where});
  }
  result<void> outcome{};
  out.clear();
  Codec::for_each_element(held, [&](const Codec::value& element) {
    if (!outcome) {
      return;
    }
    typename F::value_type inner{};
    if (const auto read = field_from_json<Codec>(element, where, inner); !read) {
      outcome = read;
      return;
    }
    out.push_back(std::move(inner));
  });
  return outcome;
}

template<json::json_codec Codec, class F>
[[nodiscard]] auto object_from_json(const typename Codec::value& held,
                                    std::string_view where,
                                    F& out) -> result<void> {
  if (Codec::kind_of(held) != json::kind::object) {
    return fail(errc::type_mismatch, "expected a JSON object", std::string{where});
  }
  result<void> outcome{};
  out.clear();
  Codec::for_each_member(held, [&](std::string_view key, const Codec::value& member) {
    if (!outcome) {
      return;
    }
    typename F::mapped_type inner{};
    if (const auto read = field_from_json<Codec>(member, key, inner); !read) {
      outcome = read;
      return;
    }
    out.insert_or_assign(std::string{key}, std::move(inner));
  });
  return outcome;
}

template<json::json_codec Codec, class F>
[[nodiscard]] auto scalar_from_json(const typename Codec::value& held,
                                    std::string_view where,
                                    F& out) -> result<void> {
  if constexpr (std::is_same_v<F, bool>) {
    auto read = Codec::as_bool(held);
    if (!read) {
      return fail(errc::type_mismatch, "expected a JSON boolean", std::string{where});
    }
    out = *read;
    return {};
  } else if constexpr (std::is_same_v<F, std::string>) {
    auto read = Codec::as_string(held);
    if (!read) {
      return fail(errc::type_mismatch, "expected a JSON string", std::string{where});
    }
    out = std::string{*read};
    return {};
  } else if constexpr (std::is_same_v<F, double>) {
    auto read = Codec::as_double(held);
    if (!read) {
      return fail(errc::type_mismatch, "expected a JSON number", std::string{where});
    }
    out = *read;
    return {};
  } else {
    auto read = Codec::as_int(held);
    if (!read) {
      return fail(errc::type_mismatch, "expected a JSON integer", std::string{where});
    }
    if (*read < static_cast<std::int64_t>(std::numeric_limits<F>::min()) ||
        *read > static_cast<std::int64_t>(std::numeric_limits<F>::max())) {
      return fail(
          errc::out_of_range, "the integer does not fit the declared field", std::string{where});
    }
    out = static_cast<F>(*read);
    return {};
  }
}

template<json::json_codec Codec, class F>
[[nodiscard]] auto field_from_json(const typename Codec::value& held,
                                   std::string_view where,
                                   F& out) -> result<void> {
  if constexpr (is_json_optional<F>) {
    if (Codec::kind_of(held) == json::kind::null) {
      out.reset();
      return {};
    }
    typename F::value_type inner{};
    if (const auto read = field_from_json<Codec>(held, where, inner); !read) {
      return read;
    }
    out = std::move(inner);
    return {};
  } else if constexpr (is_json_vector<F>) {
    return array_from_json<Codec>(held, where, out);
  } else if constexpr (is_json_map<F>) {
    return object_from_json<Codec>(held, where, out);
  } else {
    return scalar_from_json<Codec>(held, where, out);
  }
}

}  // namespace detail

template<json::json_codec Codec, described T>
[[nodiscard]] auto to_json_value(const T& held) -> Codec::value {
  static_assert(members_supported<T>(),
                "a described type used as JSON must declare only bool, int32_t, int64_t, "
                "double, std::string, or an optional, vector or string-keyed map of those");

  auto object = Codec::make_object();
  for_each_field(held, [&object](std::string_view name, const auto& field) {
    Codec::set(object, name, detail::field_to_json<Codec>(field));
  });
  return object;
}

template<json::json_codec Codec, described T>
[[nodiscard]] auto from_json_value(const typename Codec::value& document) -> result<T> {
  static_assert(members_supported<T>(),
                "a described type used as JSON must declare only bool, int32_t, int64_t, "
                "double, std::string, or an optional, vector or string-keyed map of those");

  if (Codec::kind_of(document) != json::kind::object) {
    return fail(errc::type_mismatch, "a described type decodes from a JSON object");
  }

  T out{};
  result<void> field_error{};

  for_each_field(out, [&document, &field_error](std::string_view name, auto& field) {
    if (!field_error) {
      return;
    }
    const typename Codec::value* member = Codec::find(document, name);
    if (member == nullptr) {
      return;
    }
    field_error = detail::field_from_json<Codec>(*member, name, field);
  });

  if (!field_error) {
    return fail(field_error.error().code, field_error.error().detail, field_error.error().where);
  }
  return out;
}

}  // namespace ce::v2

// spec: SWR-BUILD-0005
namespace ce::inline v3 {
using ce::v2::from_json_value;
using ce::v2::to_json_value;
}  // namespace ce::inline v3
