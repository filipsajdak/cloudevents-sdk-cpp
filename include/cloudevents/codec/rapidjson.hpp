#pragma once

/// \file
/// \brief A `ce::json::json_codec` over RapidJSON.
///
/// \par Allocator
/// RapidJSON wants an allocator at every mutation and the concept passes none,
/// so this codec uses one shared `rapidjson::CrtAllocator`. Not the default
/// `MemoryPoolAllocator`: a pool does not return memory until it is destroyed,
/// so a pooled codec would mean a process-wide arena that only grows.
/// `CrtAllocator` frees with the value.
///
/// \par Thread safety
/// The shared allocator holds no state - every call forwards to `malloc` and
/// `free`, which the C standard requires to be thread-safe. Two threads may use
/// this codec concurrently on separate documents. They may not share one
/// `value`, which is true of every codec here.
///
/// \par Pointer invalidation
/// `find` returns a pointer into a contiguous member array. A later `set` on the
/// same object may reallocate that array and invalidate every pointer `find`
/// returned before it. `json_format` never mutates a document it is reading, so
/// this is unobservable through the SDK; a caller using the codec directly must
/// re-`find` after a `set`. nlohmann's DOM is node-stable and does not behave
/// this way, so code that is correct there can be wrong here.

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ce::inline v1::codec {

/// \brief RapidJSON as a CloudEvents JSON codec.
struct rapidjson_codec {
  /// \brief The RapidJSON value type this codec drives.
  using rj_value = rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
  /// \brief The matching document type, used only inside `parse`.
  using rj_document = rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator,
                                                 rapidjson::CrtAllocator>;
  /// \brief The DOM node the format layer passes around.
  using value = rj_value;

  // If this ever becomes a MemoryPoolAllocator, `parse` returns a value whose
  // storage the document destructor has just released.
  static_assert(std::is_same_v<rj_document::AllocatorType, rapidjson::CrtAllocator>,
                "parse() moves the root out of the document, which is sound only "
                "because the allocator is stateless");

  /// \brief The shared, stateless allocator. See the file comment.
  [[nodiscard]] static auto allocator() -> rapidjson::CrtAllocator& {
    static rapidjson::CrtAllocator instance;
    return instance;
  }

  /// \brief A never-null pointer to the view's characters.
  ///
  /// `std::string_view{}.data()` is null, and RapidJSON asserts a non-null
  /// pointer in every overload that takes one. The assertion is compiled out of
  /// a release build, which turns an empty key or an empty document into a null
  /// dereference rather than a diagnosis.
  [[nodiscard]] static constexpr auto chars_of(std::string_view text) noexcept -> const char* {
    return text.empty() ? "" : text.data();
  }

  /// \brief A JSON null.
  [[nodiscard]] static auto make_null() -> value { return value{rapidjson::kNullType}; }
  /// \brief A JSON boolean.
  [[nodiscard]] static auto make_bool(bool held) -> value { return value{held}; }
  /// \brief A JSON integer.
  [[nodiscard]] static auto make_int(std::int64_t held) -> value { return value{held}; }
  /// \brief A JSON real.
  [[nodiscard]] static auto make_double(double held) -> value { return value{held}; }
  /// \brief A JSON string, copying the text.
  [[nodiscard]] static auto make_string(std::string_view held) -> value {
    return value{chars_of(held), static_cast<rapidjson::SizeType>(held.size()), allocator()};
  }
  /// \brief An empty JSON array.
  [[nodiscard]] static auto make_array() -> value { return value{rapidjson::kArrayType}; }
  /// \brief An empty JSON object.
  [[nodiscard]] static auto make_object() -> value { return value{rapidjson::kObjectType}; }

  /// \brief Insert or replace a member. RapidJSON has no insert-or-assign.
  static void set(value& object, std::string_view key, value member) {
    // A non-owning probe: the lookup copies nothing.
    const value probe{chars_of(key), static_cast<rapidjson::SizeType>(key.size())};
    if (auto found = object.FindMember(probe); found != object.MemberEnd()) {
      found->value = std::move(member);
      return;
    }
    value name{chars_of(key), static_cast<rapidjson::SizeType>(key.size()), allocator()};
    object.AddMember(name, member, allocator());
  }

  /// \brief Append an element to an array.
  static void push(value& array, value element) { array.PushBack(element, allocator()); }

  /// \brief Which JSON kind this value holds.
  [[nodiscard]] static auto kind_of(const value& held) -> ce::json::kind {
    switch (held.GetType()) {
      case rapidjson::kNullType: return ce::json::kind::null;
      case rapidjson::kFalseType:
      case rapidjson::kTrueType: return ce::json::kind::boolean;
      case rapidjson::kObjectType: return ce::json::kind::object;
      case rapidjson::kArrayType: return ce::json::kind::array;
      case rapidjson::kStringType: return ce::json::kind::string;
      default: break;
    }
    // Integer-ness follows the text, not the magnitude: a value past INT64_MAX
    // is still an integer, and `as_int` is what refuses it. Reporting it as
    // floating would make the format layer diagnose it as a fractional
    // extension value, which is a different and wrong complaint.
    return (held.IsInt64() || held.IsUint64()) ? ce::json::kind::integer
                                               : ce::json::kind::floating;
  }

  /// \brief Element or member count. Not defined for other kinds; returns zero.
  [[nodiscard]] static auto size_of(const value& held) -> std::size_t {
    if (held.IsArray()) {
      return held.Size();
    }
    return held.IsObject() ? held.MemberCount() : 0;
  }

  /// \brief The member, or nullptr. See the file comment on invalidation.
  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.IsObject()) {
      return nullptr;
    }
    const value probe{chars_of(key), static_cast<rapidjson::SizeType>(key.size())};
    auto found = object.FindMember(probe);
    return found == object.MemberEnd() ? nullptr : &found->value;
  }

  /// \brief The boolean, or a type mismatch.
  [[nodiscard]] static auto as_bool(const value& held) -> ce::result<bool> {
    if (!held.IsBool()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON boolean");
    }
    return held.GetBool();
  }

  /// \brief The integer, or an error.
  ///
  /// An integer past `int64` reports `out_of_range` rather than
  /// `type_mismatch`: it is an integer, and one that merely exceeds the 32-bit
  /// CloudEvents `Integer` type already reports `out_of_range`.
  [[nodiscard]] static auto as_int(const value& held) -> ce::result<std::int64_t> {
    if (held.IsInt64()) {
      return held.GetInt64();
    }
    if (held.IsUint64()) {
      return ce::fail(ce::errc::out_of_range, "JSON integer too large for int64");
    }
    return ce::fail(ce::errc::type_mismatch, "not a JSON integer");
  }

  /// \brief The number as a real.
  [[nodiscard]] static auto as_double(const value& held) -> ce::result<double> {
    if (!held.IsNumber()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON number");
    }
    return held.GetDouble();
  }

  /// \brief The string, viewing storage owned by `held`.
  [[nodiscard]] static auto as_string(const value& held) -> ce::result<std::string_view> {
    if (!held.IsString()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{held.GetString(), held.GetStringLength()};
  }

  /// \brief Visit every member of an object, in insertion order.
  template <class F>
  static void for_each_member(const value& object, F&& visit) {
    if (!object.IsObject()) {
      return;
    }
    for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
      visit(std::string_view{it->name.GetString(), it->name.GetStringLength()}, it->value);
    }
  }

  /// \brief Visit every element of an array, in order.
  template <class F>
  static void for_each_element(const value& array, F&& visit) {
    if (!array.IsArray()) {
      return;
    }
    for (auto it = array.Begin(); it != array.End(); ++it) {
      visit(*it);
    }
  }

  /// \brief Parse a document. Never throws; a syntax error is a `parse_error`.
  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    rj_document document{&allocator()};
    // The length overload: a string_view is not null-terminated.
    document.Parse(chars_of(text), text.size());
    if (document.HasParseError()) {
      return ce::fail(ce::errc::parse_error,
                      rapidjson::GetParseError_En(document.GetParseError()));
    }
    // Sound only because the allocator is stateless; see the static_assert.
    return value{std::move(static_cast<rj_value&>(document))};
  }

  /// \brief Serialize a value.
  [[nodiscard]] static auto dump(const value& held) -> std::string {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
    held.Accept(writer);
    return std::string{buffer.GetString(), buffer.GetSize()};
  }
};

static_assert(json::json_codec<rapidjson_codec>);

}  // namespace ce::inline v1::codec
