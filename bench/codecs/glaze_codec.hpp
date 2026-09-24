#pragma once

/// \file
/// \brief A `ce::json::json_codec` over Glaze's generic JSON value.
///
/// `glz::generic_json<glz::num_mode::i64>`, NOT the default. Glaze's default
/// generic value stores every number as a double, which is fine for JavaScript
/// and wrong for CloudEvents: the JSON format has an Integer attribute type and
/// a fractional extension value has to be refused. In the default mode 5 and
/// 5.0 are the same value, so that rule cannot be expressed.
///
/// A user who reaches for `glz::generic` because it is the obvious type gets a
/// codec that compiles, passes a round trip, and quietly mistypes every
/// integer extension.

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <glaze/json/patch.hpp>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ce::bench {

struct glaze_codec {
  using value = glz::generic_json<glz::num_mode::i64>;

  /// Every factory assigns the variant alternative rather than calling the
  /// converting constructor. `value{std::string{...}}` does compile, and
  /// produces a one-element ARRAY holding the string - the document comes out
  /// as `"specversion":["1.0"]`, encodes without complaint, and fails to decode
  /// again. Naming the alternative is the only form that says what is meant.
  template <class T>
  static auto hold(T&& alternative) -> value {
    value out;
    out.data = std::forward<T>(alternative);
    return out;
  }

  static auto make_null() -> value { return hold(nullptr); }
  static auto make_bool(bool v) -> value { return hold(v); }
  static auto make_int(std::int64_t v) -> value { return hold(v); }
  static auto make_double(double v) -> value { return hold(v); }
  static auto make_string(std::string_view v) -> value { return hold(std::string{v}); }
  static auto make_array() -> value { return hold(value::array_t{}); }
  static auto make_object() -> value { return hold(value::object_t{}); }

  static void set(value& object, std::string_view key, value member) {
    auto& map = object.get_object();
    if (auto found = map.find(key); found != map.end()) {
      found->second = std::move(member);
      return;
    }
    map.insert(std::pair<std::string, value>{std::string{key}, std::move(member)});
  }

  static void push(value& array, value element) {
    array.get_array().push_back(std::move(element));
  }

  static auto kind_of(const value& v) -> ce::json::kind {
    if (v.is_null()) return ce::json::kind::null;
    if (v.is_boolean()) return ce::json::kind::boolean;
    if (v.is_string()) return ce::json::kind::string;
    if (v.is_array()) return ce::json::kind::array;
    if (v.is_object()) return ce::json::kind::object;
    return v.is_int64() ? ce::json::kind::integer : ce::json::kind::floating;
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
    const auto& map = object.get_object();
    auto found = map.find(key);
    return found == map.end() ? nullptr : &found->second;
  }

  static auto as_bool(const value& v) -> ce::result<bool> {
    if (!v.is_boolean()) {
      return ce::fail(ce::errc::type_mismatch, "not a boolean");
    }
    return v.get_boolean();
  }
  static auto as_int(const value& v) -> ce::result<std::int64_t> {
    if (!v.is_int64()) {
      return ce::fail(ce::errc::type_mismatch, "not an integer");
    }
    return std::get<std::int64_t>(v.data);
  }
  static auto as_double(const value& v) -> ce::result<double> {
    if (v.is_int64()) {
      return static_cast<double>(std::get<std::int64_t>(v.data));
    }
    if (!v.is_number()) {
      return ce::fail(ce::errc::type_mismatch, "not a number");
    }
    return v.get_number();
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
      visit(std::string_view{entry.first}, entry.second);
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
    value out;
    if (auto error = glz::read_json(out, text)) {
      return ce::fail(ce::errc::parse_error, glz::format_error(error, text));
    }
    return out;
  }

  static auto dump(const value& v) -> std::string {
    std::string out;
    if (auto error = glz::write_json(v, out)) {
      return {};
    }
    return out;
  }

  static constexpr std::string_view identity = "io.cloudevents.cpp.bench.glaze";
  static auto equal(const value& left, const value& right) -> bool {
    return glz::equal(left, right);
  }
  static auto copy(const value& v) -> value { return v; }
};

}  // namespace ce::bench
