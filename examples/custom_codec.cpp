/// \file
/// \brief Use the SDK with a JSON library it has never heard of.
///
/// The format layer names only `Codec::` statics, so supplying one is the whole
/// integration. Nothing here includes nlohmann.

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/result.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {

/// A deliberately small JSON value: enough to satisfy `ce::json::json_codec`.
struct value {
  ce::json::kind tag = ce::json::kind::null;
  bool boolean = false;
  std::int64_t integer = 0;
  double number = 0.0;
  std::string text{};
  std::vector<value> elements{};
  std::vector<std::pair<std::string, value>> members{};
};

/// The codec is a set of statics. It holds no state, which is why the SDK takes
/// it as a template parameter rather than as an argument.
struct codec {
  using value = demo::value;

  static auto make_null() -> value { return value{}; }
  static auto make_bool(bool v) -> value {
    return value{.tag = ce::json::kind::boolean, .boolean = v};
  }
  static auto make_int(std::int64_t v) -> value {
    return value{.tag = ce::json::kind::integer, .integer = v};
  }
  static auto make_double(double v) -> value {
    return value{.tag = ce::json::kind::floating, .number = v};
  }
  static auto make_string(std::string_view v) -> value {
    return value{.tag = ce::json::kind::string, .text = std::string{v}};
  }
  static auto make_array() -> value { return value{.tag = ce::json::kind::array}; }
  static auto make_object() -> value { return value{.tag = ce::json::kind::object}; }

  static void set(value& object, std::string_view key, value member) {
    for (auto& [existing, stored] : object.members) {
      if (existing == key) {
        stored = std::move(member);
        return;
      }
    }
    object.members.emplace_back(std::string{key}, std::move(member));
  }
  static void push(value& array, value element) { array.elements.push_back(std::move(element)); }

  static auto kind_of(const value& v) -> ce::json::kind { return v.tag; }
  static auto size_of(const value& v) -> std::size_t {
    return v.tag == ce::json::kind::array ? v.elements.size() : v.members.size();
  }

  /// Returning a pointer rather than an optional is what lets the format layer
  /// tell "absent" from "present and null", which the data rules depend on.
  static auto find(const value& object, std::string_view key) -> const value* {
    for (const auto& [existing, stored] : object.members) {
      if (existing == key) {
        return &stored;
      }
    }
    return nullptr;
  }

  static auto as_bool(const value& v) -> ce::result<bool> {
    if (v.tag != ce::json::kind::boolean) {
      return ce::fail(ce::errc::type_mismatch, "not a boolean");
    }
    return v.boolean;
  }
  static auto as_int(const value& v) -> ce::result<std::int64_t> {
    if (v.tag != ce::json::kind::integer) {
      return ce::fail(ce::errc::type_mismatch, "not an integer");
    }
    return v.integer;
  }
  static auto as_double(const value& v) -> ce::result<double> {
    if (v.tag == ce::json::kind::integer) {
      return static_cast<double>(v.integer);
    }
    if (v.tag != ce::json::kind::floating) {
      return ce::fail(ce::errc::type_mismatch, "not a number");
    }
    return v.number;
  }
  static auto as_string(const value& v) -> ce::result<std::string_view> {
    if (v.tag != ce::json::kind::string) {
      return ce::fail(ce::errc::type_mismatch, "not a string");
    }
    return std::string_view{v.text};
  }

  template <class F>
  static void for_each_member(const value& object, F&& visit) {
    for (const auto& [key, stored] : object.members) {
      visit(std::string_view{key}, stored);
    }
  }
  template <class F>
  static void for_each_element(const value& array, F&& visit) {
    for (const auto& element : array.elements) {
      visit(element);
    }
  }

  static auto dump(const value& v) -> std::string;
  static auto parse(std::string_view text) -> ce::result<value>;
};

}  // namespace demo

// Included after the codec so the example reads top to bottom; the
// implementations are ordinary JSON serialisation and are not the point.
#include "custom_codec_impl.inc"

int main() {
  // The concept is what the format layer checks. A codec that does not satisfy
  // it fails here, naming the missing operation, rather than deep inside the
  // format templates.
  static_assert(ce::json::json_codec<demo::codec>);

  using format = ce::json_format<demo::codec>;

  ce::event subject{
      .id = "A234-1234-1234",
      .source = ce::uri_ref{"https://example.test/orders"},
      .type = "com.example.order.placed",
  };
  subject.datacontenttype = "application/json";
  subject.data = ce::json_text{.raw = R"({"total":42})"};

  auto encoded = format::encode(subject);
  if (!encoded) {
    std::fprintf(stderr, "encode: %s\n", encoded.error().detail.c_str());
    return 1;
  }
  std::printf("encoded with a third-party codec:\n%s\n\n", encoded->c_str());

  auto decoded = format::decode(*encoded);
  if (!decoded) {
    std::fprintf(stderr, "decode: %s\n", decoded.error().detail.c_str());
    return 1;
  }
  std::printf("round-tripped: %s\n", (*decoded == subject) ? "identical" : "DIFFERENT");
  return (*decoded == subject) ? 0 : 1;
}
