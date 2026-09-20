#pragma once

/// \file
/// \brief A `ce::json::json_codec` over Boost.JSON.
///
/// The closest fit of the libraries measured: `boost::json::value` is
/// self-contained, carries its own memory resource, and an object member is
/// addressable, so every operation the concept asks for is one call.
///
/// \par Exceptions
/// Boost.JSON's `as_object()`, `as_array()` and the scalar `as_*` accessors
/// THROW `boost::system::system_error` on a type mismatch. The `get_*` forms
/// assert instead, and this codec uses those exclusively, because the SDK
/// supports `-fno-exceptions` (SPEC section 9, D4). The `as_` spellings must not
/// be reintroduced: they compile, and every test passes with them in a build
/// that has exceptions.
///
/// Each `get_*` call here is guarded by the matching `is_*` test, or is reached
/// only on a value this codec has just produced with `make_object`/`make_array`,
/// so the assertion is not reachable through `json_format`.
///
/// \par Linking
/// Boost.JSON is a COMPILED library, unlike every other dependency of this SDK.
/// The compiler and standard library must match the ones Boost was built with:
/// on macOS, a Homebrew Boost is built against libc++, so a GCC/libstdc++ build
/// will compile this header and then fail to link with undefined symbols whose
/// mangling differs. That is an ABI mismatch, not a defect in the codec.

#include <cloudevents/detail/config.hpp>

// Boost infers BOOST_NO_EXCEPTIONS from -fno-exceptions and then requires the
// PROGRAM to define boost::throw_exception. That function decides what happens
// when Boost reports an error - terminate, abort, longjmp - which is an
// application's policy and not an SDK's to choose. Defining it here would make
// that decision for every consumer, silently.
//
// So this codec refuses the combination instead of half-supporting it. The rest
// of the SDK builds without exceptions; only this codec does not, because only
// this codec has a compiled third-party library behind it.
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

/// \brief Boost.JSON as a CloudEvents JSON codec.
struct boost_json_codec {
  /// \brief The DOM node the format layer passes around.
  using value = boost::json::value;

  /// \brief A JSON null.
  [[nodiscard]] static auto make_null() -> value { return value{nullptr}; }
  /// \brief A JSON boolean.
  [[nodiscard]] static auto make_bool(bool held) -> value { return value{held}; }
  /// \brief A JSON integer.
  [[nodiscard]] static auto make_int(std::int64_t held) -> value { return value{held}; }
  /// \brief A JSON real.
  [[nodiscard]] static auto make_double(double held) -> value { return value{held}; }
  /// \brief A JSON string, copying the text.
  [[nodiscard]] static auto make_string(std::string_view held) -> value {
    return value{boost::json::string{held}};
  }
  /// \brief An empty JSON array.
  [[nodiscard]] static auto make_array() -> value { return value{boost::json::array{}}; }
  /// \brief An empty JSON object.
  [[nodiscard]] static auto make_object() -> value { return value{boost::json::object{}}; }

  /// \brief Insert or replace a member. The object must be one.
  static void set(value& object, std::string_view key, value member) {
    // get_object, not as_object: the latter throws. See the file comment.
    object.get_object().insert_or_assign(key, std::move(member));
  }

  /// \brief Append an element. The array must be one.
  static void push(value& array, value element) {
    array.get_array().push_back(std::move(element));
  }

  /// \brief Which JSON kind this value holds.
  [[nodiscard]] static auto kind_of(const value& held) -> ce::json::kind {
    switch (held.kind()) {
      case boost::json::kind::null: return ce::json::kind::null;
      case boost::json::kind::bool_: return ce::json::kind::boolean;
      // Integer-ness follows the text, not the magnitude: a uint64 past
      // INT64_MAX is still an integer, and as_int is what refuses it.
      case boost::json::kind::int64:
      case boost::json::kind::uint64: return ce::json::kind::integer;
      case boost::json::kind::double_: return ce::json::kind::floating;
      case boost::json::kind::string: return ce::json::kind::string;
      case boost::json::kind::array: return ce::json::kind::array;
      case boost::json::kind::object: break;
    }
    return ce::json::kind::object;
  }

  /// \brief Element or member count. Not defined for other kinds; returns zero.
  [[nodiscard]] static auto size_of(const value& held) -> std::size_t {
    if (held.is_array()) {
      return held.get_array().size();
    }
    return held.is_object() ? held.get_object().size() : 0;
  }

  /// \brief The member, or nullptr. Boost.JSON hands back the address natively.
  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.is_object()) {
      return nullptr;
    }
    return object.get_object().if_contains(key);
  }

  /// \brief The boolean, or a type mismatch.
  [[nodiscard]] static auto as_bool(const value& held) -> ce::result<bool> {
    if (!held.is_bool()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON boolean");
    }
    return held.get_bool();
  }

  /// \brief The integer, or an error.
  ///
  /// An integer past `int64` reports `out_of_range` rather than
  /// `type_mismatch`: it is an integer, and one that merely exceeds the 32-bit
  /// CloudEvents `Integer` type already reports `out_of_range`.
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

  /// \brief The number as a real.
  [[nodiscard]] static auto as_double(const value& held) -> ce::result<double> {
    if (!held.is_number()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON number");
    }
    return held.to_number<double>();
  }

  /// \brief The string, viewing storage owned by `held`.
  [[nodiscard]] static auto as_string(const value& held) -> ce::result<std::string_view> {
    if (!held.is_string()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{held.get_string()};
  }

  /// \brief Visit every member of an object, in insertion order.
  template <class F>
  static void for_each_member(const value& object, F&& visit) {
    if (!object.is_object()) {
      return;
    }
    for (const auto& entry : object.get_object()) {
      visit(std::string_view{entry.key()}, entry.value());
    }
  }

  /// \brief Visit every element of an array, in order.
  template <class F>
  static void for_each_element(const value& array, F&& visit) {
    if (!array.is_array()) {
      return;
    }
    for (const auto& element : array.get_array()) {
      visit(element);
    }
  }

  /// \brief Parse a document. Never throws; the error-code overload is used.
  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    boost::system::error_code error;
    auto parsed = boost::json::parse(text, error);
    if (error) {
      return ce::fail(ce::errc::parse_error, error.message());
    }
    return parsed;
  }

  /// \brief Serialize a value.
  [[nodiscard]] static auto dump(const value& held) -> std::string {
    return boost::json::serialize(held);
  }
};

static_assert(json::json_codec<boost_json_codec>);

}  // namespace ce::inline v1::codec
