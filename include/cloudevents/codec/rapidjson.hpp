#pragma once

// spec: SWR-JSON-0036
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

namespace ce::v1::codec {

struct rapidjson_codec {
  using rj_value = rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
  using rj_document = rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator,
                                                 rapidjson::CrtAllocator>;
  using value = rj_value;

  static_assert(std::is_same_v<rj_document::AllocatorType, rapidjson::CrtAllocator>,
                "parse() moves the root out of the document, which is sound only "
                "because the allocator is stateless");

  [[nodiscard]] static auto allocator() -> rapidjson::CrtAllocator& {
    static rapidjson::CrtAllocator instance;
    return instance;
  }

  [[nodiscard]] static constexpr auto chars_of(std::string_view text) noexcept -> const char* {
    return text.empty() ? "" : text.data();
  }

  [[nodiscard]] static auto make_null() -> value { return value{rapidjson::kNullType}; }
  [[nodiscard]] static auto make_bool(bool held) -> value { return value{held}; }
  [[nodiscard]] static auto make_int(std::int64_t held) -> value { return value{held}; }
  [[nodiscard]] static auto make_double(double held) -> value { return value{held}; }
  [[nodiscard]] static auto make_string(std::string_view held) -> value {
    return value{chars_of(held), static_cast<rapidjson::SizeType>(held.size()), allocator()};
  }
  [[nodiscard]] static auto make_array() -> value { return value{rapidjson::kArrayType}; }
  [[nodiscard]] static auto make_object() -> value { return value{rapidjson::kObjectType}; }

  static void set(value& object, std::string_view key, value member) {
    const value probe{chars_of(key), static_cast<rapidjson::SizeType>(key.size())};
    if (auto found = object.FindMember(probe); found != object.MemberEnd()) {
      found->value = std::move(member);
      return;
    }
    value name{chars_of(key), static_cast<rapidjson::SizeType>(key.size()), allocator()};
    object.AddMember(name, member, allocator());
  }

  static void push(value& array, value element) { array.PushBack(element, allocator()); }

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
    return (held.IsInt64() || held.IsUint64()) ? ce::json::kind::integer
                                               : ce::json::kind::floating;
  }

  [[nodiscard]] static auto size_of(const value& held) -> std::size_t {
    if (held.IsArray()) {
      return held.Size();
    }
    return held.IsObject() ? held.MemberCount() : 0;
  }

  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.IsObject()) {
      return nullptr;
    }
    const value probe{chars_of(key), static_cast<rapidjson::SizeType>(key.size())};
    auto found = object.FindMember(probe);
    return found == object.MemberEnd() ? nullptr : &found->value;
  }

  [[nodiscard]] static auto as_bool(const value& held) -> ce::result<bool> {
    if (!held.IsBool()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON boolean");
    }
    return held.GetBool();
  }

  [[nodiscard]] static auto as_int(const value& held) -> ce::result<std::int64_t> {
    if (held.IsInt64()) {
      return held.GetInt64();
    }
    if (held.IsUint64()) {
      return ce::fail(ce::errc::out_of_range, "JSON integer too large for int64");
    }
    return ce::fail(ce::errc::type_mismatch, "not a JSON integer");
  }

  [[nodiscard]] static auto as_double(const value& held) -> ce::result<double> {
    if (!held.IsNumber()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON number");
    }
    return held.GetDouble();
  }

  [[nodiscard]] static auto as_string(const value& held) -> ce::result<std::string_view> {
    if (!held.IsString()) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{held.GetString(), held.GetStringLength()};
  }

  template <class F>
  static void for_each_member(const value& object, F&& visit) {
    if (!object.IsObject()) {
      return;
    }
    for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
      visit(std::string_view{it->name.GetString(), it->name.GetStringLength()}, it->value);
    }
  }

  template <class F>
  static void for_each_element(const value& array, F&& visit) {
    if (!array.IsArray()) {
      return;
    }
    for (auto it = array.Begin(); it != array.End(); ++it) {
      visit(*it);
    }
  }
  /// Visits each element as a mutable reference, so a caller that owns the
  /// array can move out of its elements.
  template <class F>
  static void for_each_mutable_element(value& array, F&& visit) {
    if (!array.IsArray()) {
      return;
    }
    for (auto it = array.Begin(); it != array.End(); ++it) {
      visit(*it);
    }
  }

  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    rj_document document{&allocator()};
    document.Parse(chars_of(text), text.size());
    if (document.HasParseError()) {
      return ce::fail(ce::errc::parse_error,
                      rapidjson::GetParseError_En(document.GetParseError()));
    }
    return value{std::move(static_cast<rj_value&>(document))};
  }

  [[nodiscard]] static auto dump(const value& held) -> std::string {
    struct string_output {
      using Ch = char;

      std::string* text;

      void Put(Ch character) { text->push_back(character); }
      void Flush() {}
    };

    std::string text;
    text.reserve(rapidjson::StringBuffer::kDefaultCapacity);
    string_output output{.text = &text};
    rapidjson::Writer<string_output> writer{output};
    held.Accept(writer);
    return text;
  }

  // spec: SWR-JSON-0039
  static constexpr std::string_view identity = "io.cloudevents.cpp.codec.rapidjson";
  [[nodiscard]] static auto equal(const value& left, const value& right) -> bool {
    return left == right;
  }
  [[nodiscard]] static auto copy(const value& held) -> value { return value{held, allocator()}; }
  [[nodiscard]] static auto extract(value& object, std::string_view key) -> value {
    if (!object.IsObject()) {
      return make_null();
    }
    const value probe{chars_of(key), static_cast<rapidjson::SizeType>(key.size())};
    auto found = object.FindMember(probe);
    return found == object.MemberEnd() ? make_null() : value{std::move(found->value)};
  }
};

static_assert(json::json_codec<rapidjson_codec>);
static_assert(ce::v3::json::json_codec<rapidjson_codec>);

}  // namespace ce::v1::codec

namespace ce::v2 {
namespace codec = ce::v1::codec;
}  // namespace ce::v2

// spec: SWR-BUILD-0005
namespace ce::inline v3 {
namespace codec = ce::v1::codec;
}  // namespace ce::inline v3
