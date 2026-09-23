#pragma once

#include <cloudevents/detail/config.hpp>

#if !CE_HAS_EXCEPTIONS
#  error "cloudevents: the Boost.JSON codec needs exceptions. Boost requires the program to define boost::throw_exception under -fno-exceptions, which is an application policy decision this SDK will not make for you. Use another codec, or define boost::throw_exception yourself."
#endif

#include <boost/json.hpp>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ce::inline v1::codec {

struct boost_json_codec {
  using value = boost::json::value;

  [[nodiscard]] static auto make_null() -> value { return value(nullptr); }
  [[nodiscard]] static auto make_bool(bool held) -> value { return value(held); }
  [[nodiscard]] static auto make_int(std::int64_t held) -> value { return value(held); }
  [[nodiscard]] static auto make_double(double held) -> value { return value(held); }
  [[nodiscard]] static auto make_string(std::string_view held) -> value {
    return value(boost::json::string(held));
  }
  [[nodiscard]] static auto make_array() -> value { return value(boost::json::array()); }
  [[nodiscard]] static auto make_object() -> value { return value(boost::json::object()); }

  static void set(value& object, std::string_view key, value member) {
    object.get_object().insert_or_assign(key, std::move(member));
  }

  static void push(value& array, value element) {
    array.get_array().push_back(std::move(element));
  }

  [[nodiscard]] static auto kind_of(const value& held) -> ce::json::kind {
    switch (held.kind()) {
      case boost::json::kind::null: return ce::json::kind::null;
      case boost::json::kind::bool_: return ce::json::kind::boolean;
      case boost::json::kind::int64:
      case boost::json::kind::uint64: return ce::json::kind::integer;
      case boost::json::kind::double_: return ce::json::kind::floating;
      case boost::json::kind::string: return ce::json::kind::string;
      case boost::json::kind::array: return ce::json::kind::array;
      case boost::json::kind::object: break;
    }
    return ce::json::kind::object;
  }

  [[nodiscard]] static auto size_of(const value& held) -> std::size_t {
    if (held.is_array()) {
      return held.get_array().size();
    }
    return held.is_object() ? held.get_object().size() : 0;
  }

  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.is_object()) {
      return nullptr;
    }
    return object.get_object().if_contains(key);
  }

  [[nodiscard]] static auto as_bool(const value& held) -> ce::result<bool> {
    if (!held.is_bool()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON boolean");
    }
    return held.get_bool();
  }

  [[nodiscard]] static auto as_int(const value& held) -> ce::result<std::int64_t> {
    if (held.is_int64()) {
      return held.get_int64();
    }
    if (held.is_uint64()) {
      const auto unsigned_held = held.get_uint64();
      if (unsigned_held > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return ce::fail(ce::errc::out_of_range, "JSON integer too large for int64");
      }
      return static_cast<std::int64_t>(unsigned_held);
    }
    return ce::fail(ce::errc::type_mismatch, "not a JSON integer");
  }

  [[nodiscard]] static auto as_double(const value& held) -> ce::result<double> {
    if (!held.is_number()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON number");
    }
    return held.to_number<double>();
  }

  [[nodiscard]] static auto as_string(const value& held) -> ce::result<std::string_view> {
    if (!held.is_string()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{held.get_string()};
  }

  template <class F>
  static void for_each_member(const value& object, F&& visit) {
    if (!object.is_object()) {
      return;
    }
    for (const auto& entry : object.get_object()) {
      visit(std::string_view{entry.key()}, entry.value());
    }
  }

  template <class F>
  static void for_each_element(const value& array, F&& visit) {
    if (!array.is_array()) {
      return;
    }
    for (const auto& element : array.get_array()) {
      visit(element);
    }
  }

  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    boost::system::error_code error;
    auto parsed = boost::json::parse(text, error);
    if (error) {
      return ce::fail(ce::errc::parse_error, error.message());
    }
    return parsed;
  }

  [[nodiscard]] static auto dump(const value& held) -> std::string {
    return boost::json::serialize(held);
  }
};

static_assert(json::json_codec<boost_json_codec>);

}  // namespace ce::inline v1::codec
