#pragma once

/// \file
/// \brief The nlohmann/json codec. The only header in the SDK that names nlohmann.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::codec {

struct nlohmann_codec {
  using value = nlohmann::json;

  /// \brief Parse without exceptions.
  ///
  /// nlohmann's default parse throws, which the SDK's error model forbids, so
  /// this uses the non-throwing overload and maps a discarded result to a typed
  /// error.
  [[nodiscard]] static auto parse(std::string_view text) -> result<value> {
    auto parsed = value::parse(text, nullptr, false, false);
    if (parsed.is_discarded()) {
      return fail(errc::parse_error, "not well-formed JSON");
    }
    return parsed;
  }

  [[nodiscard]] static auto dump(const value& subject) -> std::string { return subject.dump(); }

  [[nodiscard]] static auto make_null() -> value { return value(nullptr); }
  [[nodiscard]] static auto make_bool(bool boolean) -> value { return value(boolean); }
  [[nodiscard]] static auto make_int(std::int64_t integer) -> value { return value(integer); }
  [[nodiscard]] static auto make_double(double number) -> value { return value(number); }
  [[nodiscard]] static auto make_string(std::string_view text) -> value {
    return value(std::string{text});
  }
  [[nodiscard]] static auto make_array() -> value { return value::array(); }
  [[nodiscard]] static auto make_object() -> value { return value::object(); }

  static void set(value& object, std::string_view key, value member) {
    object[std::string{key}] = std::move(member);
  }

  static void push(value& array, value element) { array.push_back(std::move(element)); }

  [[nodiscard]] static auto kind_of(const value& subject) -> json::kind {
    if (subject.is_null()) {
      return json::kind::null;
    }
    if (subject.is_boolean()) {
      return json::kind::boolean;
    }
    // Checked before is_number_float, because an integer is also a number.
    if (subject.is_number_integer() || subject.is_number_unsigned()) {
      return json::kind::integer;
    }
    if (subject.is_number_float()) {
      return json::kind::floating;
    }
    if (subject.is_string()) {
      return json::kind::string;
    }
    if (subject.is_array()) {
      return json::kind::array;
    }
    return json::kind::object;
  }

  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.is_object()) {
      return nullptr;
    }
    const auto found = object.find(std::string{key});
    return found == object.end() ? nullptr : &(*found);
  }

  [[nodiscard]] static auto size_of(const value& subject) -> std::size_t { return subject.size(); }

  [[nodiscard]] static auto as_bool(const value& subject) -> result<bool> {
    if (!subject.is_boolean()) {
      return fail(errc::type_mismatch, "not a JSON boolean");
    }
    return subject.get<bool>();
  }

  [[nodiscard]] static auto as_int(const value& subject) -> result<std::int64_t> {
    if (!subject.is_number_integer() && !subject.is_number_unsigned()) {
      return fail(errc::type_mismatch, "not a JSON integer");
    }
    return subject.get<std::int64_t>();
  }

  [[nodiscard]] static auto as_double(const value& subject) -> result<double> {
    if (!subject.is_number()) {
      return fail(errc::type_mismatch, "not a JSON number");
    }
    return subject.get<double>();
  }

  /// \brief The string, viewing storage owned by `subject`.
  [[nodiscard]] static auto as_string(const value& subject) -> result<std::string_view> {
    if (!subject.is_string()) {
      return fail(errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{subject.get_ref<const std::string&>()};
  }

  template <class F>
  static void for_each_member(const value& object, F visit) {
    for (const auto& entry : object.items()) {
      visit(std::string_view{entry.key()}, entry.value());
    }
  }

  template <class F>
  static void for_each_element(const value& array, F visit) {
    for (const auto& element : array) {
      visit(element);
    }
  }
};

static_assert(json::json_codec<nlohmann_codec>);

}  // namespace ce::inline v1::codec
