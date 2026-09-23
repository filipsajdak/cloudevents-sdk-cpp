#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-JSON-0008
namespace ce::inline v1::codec {

struct nlohmann_codec {
  using value = nlohmann::json;

  // spec: SWR-JSON-0007
  [[nodiscard]] static auto parse(std::string_view text) -> result<value> {
    auto parsed = value::parse(text, nullptr, false, false);
    if (parsed.is_discarded()) {
      return fail(errc::parse_error, "not well-formed JSON");
    }
    return parsed;
  }

  [[nodiscard]] static auto dump(const value& held) -> std::string { return held.dump(); }

  // NOLINTBEGIN(modernize-return-braced-init-list)
  [[nodiscard]] static auto make_null() -> value { return value(nullptr); }
  [[nodiscard]] static auto make_bool(bool boolean) -> value { return value(boolean); }
  [[nodiscard]] static auto make_int(std::int64_t integer) -> value { return value(integer); }
  [[nodiscard]] static auto make_double(double number) -> value { return value(number); }
  [[nodiscard]] static auto make_string(std::string_view text) -> value {
    return value(std::string{text});
  }
  // NOLINTEND(modernize-return-braced-init-list)
  [[nodiscard]] static auto make_array() -> value { return value::array(); }
  [[nodiscard]] static auto make_object() -> value { return value::object(); }

  static void set(value& object, std::string_view key, value member) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    object[std::string{key}] = std::move(member);
  }

  static void push(value& array, value element) { array.push_back(std::move(element)); }

  [[nodiscard]] static auto kind_of(const value& held) -> json::kind {
    if (held.is_null()) {
      return json::kind::null;
    }
    if (held.is_boolean()) {
      return json::kind::boolean;
    }
    if (held.is_number_integer() || held.is_number_unsigned()) {
      return json::kind::integer;
    }
    if (held.is_number_float()) {
      return json::kind::floating;
    }
    if (held.is_string()) {
      return json::kind::string;
    }
    if (held.is_array()) {
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

  [[nodiscard]] static auto size_of(const value& held) -> std::size_t { return held.size(); }

  [[nodiscard]] static auto as_bool(const value& held) -> result<bool> {
    if (!held.is_boolean()) {
      return fail(errc::type_mismatch, "not a JSON boolean");
    }
    return held.get<bool>();
  }

  // spec: SWR-JSON-0033
  [[nodiscard]] static auto as_int(const value& held) -> result<std::int64_t> {
    if (held.is_number_unsigned()) {
      const auto unsigned_value = held.get<std::uint64_t>();
      if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return fail(errc::out_of_range, "JSON integer too large for int64");
      }
      return static_cast<std::int64_t>(unsigned_value);
    }
    if (held.is_number_integer()) {
      return held.get<std::int64_t>();
    }
    return fail(errc::type_mismatch, "not a JSON integer");
  }

  [[nodiscard]] static auto as_double(const value& held) -> result<double> {
    if (!held.is_number()) {
      return fail(errc::type_mismatch, "not a JSON number");
    }
    return held.get<double>();
  }

  [[nodiscard]] static auto as_string(const value& held) -> result<std::string_view> {
    if (!held.is_string()) {
      return fail(errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{held.get_ref<const std::string&>()};
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
