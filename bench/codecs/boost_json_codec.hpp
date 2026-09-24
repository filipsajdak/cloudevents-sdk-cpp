#pragma once

/// \file
/// \brief A `ce::json::json_codec` over Boost.JSON.
///
/// The closest fit of the four. `boost::json::value` is self-contained, carries
/// its own memory resource, and an object member is addressable, so every
/// operation the concept asks for is one call.

#include <boost/json.hpp>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ce::bench {

struct boost_json_codec {
  using value = boost::json::value;

  static auto make_null() -> value { return value{nullptr}; }
  static auto make_bool(bool v) -> value { return value{v}; }
  static auto make_int(std::int64_t v) -> value { return value{v}; }
  static auto make_double(double v) -> value { return value{v}; }
  static auto make_string(std::string_view v) -> value {
    return value{boost::json::string{v}};
  }
  static auto make_array() -> value { return value{boost::json::array{}}; }
  static auto make_object() -> value { return value{boost::json::object{}}; }

  static void set(value& object, std::string_view key, value member) {
    object.as_object().insert_or_assign(key, std::move(member));
  }

  static void push(value& array, value element) {
    array.as_array().push_back(std::move(element));
  }

  static auto kind_of(const value& v) -> ce::json::kind {
    switch (v.kind()) {
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

  static auto size_of(const value& v) -> std::size_t {
    if (v.is_array()) {
      return v.get_array().size();
    }
    return v.is_object() ? v.get_object().size() : 0;
  }

  static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.is_object()) {
      return nullptr;
    }
    return object.get_object().if_contains(key);
  }

  static auto as_bool(const value& v) -> ce::result<bool> {
    if (!v.is_bool()) {
      return ce::fail(ce::errc::type_mismatch, "not a boolean");
    }
    return v.get_bool();
  }
  static auto as_int(const value& v) -> ce::result<std::int64_t> {
    if (v.is_int64()) {
      return v.get_int64();
    }
    if (v.is_uint64() && v.get_uint64() <= static_cast<std::uint64_t>(INT64_MAX)) {
      return static_cast<std::int64_t>(v.get_uint64());
    }
    return ce::fail(ce::errc::type_mismatch, "not an integer");
  }
  static auto as_double(const value& v) -> ce::result<double> {
    if (!v.is_number()) {
      return ce::fail(ce::errc::type_mismatch, "not a number");
    }
    return v.to_number<double>();
  }
  static auto as_string(const value& v) -> ce::result<std::string_view> {
    if (!v.is_string()) {
      return ce::fail(ce::errc::type_mismatch, "not a string");
    }
    return std::string_view{v.get_string()};
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

  static auto parse(std::string_view text) -> ce::result<value> {
    boost::system::error_code error;
    auto parsed = boost::json::parse(text, error);
    if (error) {
      return ce::fail(ce::errc::parse_error, error.message());
    }
    return parsed;
  }

  static auto dump(const value& v) -> std::string { return boost::json::serialize(v); }

  static constexpr std::string_view identity = "io.cloudevents.cpp.bench.boost_json";
  static auto equal(const value& left, const value& right) -> bool { return left == right; }
  static auto copy(const value& v) -> value { return v; }
};

}  // namespace ce::bench
