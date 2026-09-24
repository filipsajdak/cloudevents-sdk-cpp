#pragma once

/// \file
/// \brief A `ce::json::json_codec` over RapidJSON.
///
/// CrtAllocator, not the default MemoryPoolAllocator. The pool allocator does
/// not return memory until it is destroyed, and the codec concept has nowhere
/// to put one, so a pooled implementation would need a process-wide arena that
/// only grows. CrtAllocator frees with the value, which is what every other
/// codec here does, and is the only setting that makes the comparison fair.

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ce::bench {

struct rapidjson_codec {
  using rj_value = rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::CrtAllocator>;
  using rj_document = rapidjson::GenericDocument<rapidjson::UTF8<>, rapidjson::CrtAllocator,
                                                 rapidjson::CrtAllocator>;
  using value = rj_value;

  /// RapidJSON wants an allocator at every mutation and the concept passes
  /// none. CrtAllocator is stateless, so one shared instance is not shared
  /// state in any meaningful sense.
  static auto allocator() -> rapidjson::CrtAllocator& {
    static rapidjson::CrtAllocator instance;
    return instance;
  }

  static auto make_null() -> value { return value{rapidjson::kNullType}; }
  static auto make_bool(bool v) -> value { return value{v}; }
  static auto make_int(std::int64_t v) -> value { return value{v}; }
  static auto make_double(double v) -> value { return value{v}; }
  static auto make_string(std::string_view v) -> value {
    return value{v.data(), static_cast<rapidjson::SizeType>(v.size()), allocator()};
  }
  static auto make_array() -> value { return value{rapidjson::kArrayType}; }
  static auto make_object() -> value { return value{rapidjson::kObjectType}; }

  static void set(value& object, std::string_view key, value member) {
    const value probe{key.data(), static_cast<rapidjson::SizeType>(key.size())};
    if (auto found = object.FindMember(probe); found != object.MemberEnd()) {
      found->value = std::move(member);
      return;
    }
    value name{key.data(), static_cast<rapidjson::SizeType>(key.size()), allocator()};
    object.AddMember(name, member, allocator());
  }

  static void push(value& array, value element) { array.PushBack(element, allocator()); }

  static auto kind_of(const value& v) -> ce::json::kind {
    switch (v.GetType()) {
      case rapidjson::kNullType: return ce::json::kind::null;
      case rapidjson::kFalseType:
      case rapidjson::kTrueType: return ce::json::kind::boolean;
      case rapidjson::kObjectType: return ce::json::kind::object;
      case rapidjson::kArrayType: return ce::json::kind::array;
      case rapidjson::kStringType: return ce::json::kind::string;
      default: break;
    }
    // A number is an integer only when it holds one, so 1.5 stays floating and
    // the format layer can refuse a fractional extension value.
    return v.IsInt64() ? ce::json::kind::integer : ce::json::kind::floating;
  }

  static auto size_of(const value& v) -> std::size_t {
    if (v.IsArray()) {
      return v.Size();
    }
    return v.IsObject() ? v.MemberCount() : 0;
  }

  static auto find(const value& object, std::string_view key) -> const value* {
    if (!object.IsObject()) {
      return nullptr;
    }
    const value probe{key.data(), static_cast<rapidjson::SizeType>(key.size())};
    auto found = object.FindMember(probe);
    return found == object.MemberEnd() ? nullptr : &found->value;
  }

  static auto as_bool(const value& v) -> ce::result<bool> {
    if (!v.IsBool()) {
      return ce::fail(ce::errc::type_mismatch, "not a boolean");
    }
    return v.GetBool();
  }
  static auto as_int(const value& v) -> ce::result<std::int64_t> {
    if (!v.IsInt64()) {
      return ce::fail(ce::errc::type_mismatch, "not an integer");
    }
    return v.GetInt64();
  }
  static auto as_double(const value& v) -> ce::result<double> {
    if (!v.IsNumber()) {
      return ce::fail(ce::errc::type_mismatch, "not a number");
    }
    return v.GetDouble();
  }
  static auto as_string(const value& v) -> ce::result<std::string_view> {
    if (!v.IsString()) {
      return ce::fail(ce::errc::type_mismatch, "not a string");
    }
    return std::string_view{v.GetString(), v.GetStringLength()};
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

  static auto parse(std::string_view text) -> ce::result<value> {
    rj_document document{&allocator()};
    document.Parse(text.data(), text.size());
    if (document.HasParseError()) {
      return ce::fail(ce::errc::parse_error,
                      rapidjson::GetParseError_En(document.GetParseError()));
    }
    return value{std::move(static_cast<rj_value&>(document))};
  }

  static auto dump(const value& v) -> std::string {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
    v.Accept(writer);
    return std::string{buffer.GetString(), buffer.GetSize()};
  }

  static constexpr std::string_view identity = "io.cloudevents.cpp.bench.rapidjson";
  static auto equal(const value& left, const value& right) -> bool { return left == right; }
  static auto copy(const value& v) -> value { return value{v, allocator()}; }
};

}  // namespace ce::bench
