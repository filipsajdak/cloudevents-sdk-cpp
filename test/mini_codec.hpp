#pragma once

/// \file
/// \brief A second codec, shaped unlike nlohmann on purpose.
///
/// Its job is to fail to compile if anything nlohmann-shaped leaks into the format
/// layer. So: no `operator[]`, no iterators, no implicit conversions, members named
/// nothing like nlohmann's, objects held in insertion order as a vector of pairs,
/// and integers kept strictly apart from doubles.

#include <algorithm>
#include <cstddef>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::test {

struct mini_codec {
  // std::vector may hold an incomplete type; std::pair may not, and libstdc++ 14
  // static_asserts on it. A forward-declared struct is what lets an object member
  // name its own value type.
  struct entry;

  struct value {
    ce::json::kind tag = ce::json::kind::null;
    bool boolean = false;
    std::int64_t integer = 0;
    double number = 0.0;
    std::string text{};
    std::vector<value> elements{};
    std::vector<entry> members{};
  };

  struct entry {
    std::string key;
    value item;
  };

  // --- construction --------------------------------------------------------
  [[nodiscard]] static auto make_null() -> value { return value{}; }
  [[nodiscard]] static auto make_bool(bool boolean) -> value {
    return value{.tag = ce::json::kind::boolean, .boolean = boolean};
  }
  [[nodiscard]] static auto make_int(std::int64_t integer) -> value {
    return value{.tag = ce::json::kind::integer, .integer = integer};
  }
  [[nodiscard]] static auto make_double(double number) -> value {
    return value{.tag = ce::json::kind::floating, .number = number};
  }
  [[nodiscard]] static auto make_string(std::string_view text) -> value {
    return value{.tag = ce::json::kind::string, .text = std::string{text}};
  }
  [[nodiscard]] static auto make_array() -> value {
    return value{.tag = ce::json::kind::array};
  }
  [[nodiscard]] static auto make_object() -> value {
    return value{.tag = ce::json::kind::object};
  }

  // --- mutation ------------------------------------------------------------
  static void set(value& object, std::string_view key, value member) {
    for (auto& [existing_key, existing] : object.members) {
      if (existing_key == key) {
        existing = std::move(member);
        return;
      }
    }
    object.members.push_back(entry{.key = std::string{key}, .item = std::move(member)});
  }

  static void push(value& array, value element) { array.elements.push_back(std::move(element)); }

  // --- inspection ----------------------------------------------------------
  [[nodiscard]] static auto kind_of(const value& subject) -> ce::json::kind { return subject.tag; }

  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    for (const auto& [existing_key, existing] : object.members) {
      if (existing_key == key) {
        return &existing;
      }
    }
    return nullptr;
  }

  [[nodiscard]] static auto size_of(const value& subject) -> std::size_t {
    return subject.tag == ce::json::kind::object ? subject.members.size() : subject.elements.size();
  }

  // --- extraction ----------------------------------------------------------
  [[nodiscard]] static auto as_bool(const value& subject) -> ce::result<bool> {
    if (subject.tag != ce::json::kind::boolean) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON boolean");
    }
    return subject.boolean;
  }

  [[nodiscard]] static auto as_int(const value& subject) -> ce::result<std::int64_t> {
    // A double never satisfies this, even when it holds an integral value.
    if (subject.tag != ce::json::kind::integer) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON integer");
    }
    return subject.integer;
  }

  [[nodiscard]] static auto as_double(const value& subject) -> ce::result<double> {
    if (subject.tag == ce::json::kind::floating) {
      return subject.number;
    }
    if (subject.tag == ce::json::kind::integer) {
      return static_cast<double>(subject.integer);
    }
    return ce::fail(ce::errc::type_mismatch, "not a JSON number");
  }

  [[nodiscard]] static auto as_string(const value& subject) -> ce::result<std::string_view> {
    if (subject.tag != ce::json::kind::string) {
      return ce::fail(ce::errc::type_mismatch, "not a JSON string");
    }
    return std::string_view{subject.text};
  }

  // --- traversal -----------------------------------------------------------
  template <class F>
  static void for_each_member(const value& object, F visit) {
    for (const auto& [key, member] : object.members) {
      visit(std::string_view{key}, member);
    }
  }

  template <class F>
  static void for_each_element(const value& array, F visit) {
    for (const auto& element : array.elements) {
      visit(element);
    }
  }

  // --- text <-> DOM --------------------------------------------------------
  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value>;
  [[nodiscard]] static auto dump(const value& subject) -> std::string;

  // --- value semantics -----------------------------------------------------
  static constexpr std::string_view identity = "io.cloudevents.cpp.test.mini";
  [[nodiscard]] static auto equal(const value& left, const value& right) -> bool;
  [[nodiscard]] static auto copy(const value& held) -> value { return held; }
  /// Moves the first member named `key` out, leaving null in its place, as
  /// `find` sees the first occurrence. Any other key returns null.
  [[nodiscard]] static auto extract(value& object, std::string_view key) -> value {
    for (auto& [existing_key, existing] : object.members) {
      if (existing_key == key) {
        return std::exchange(existing, value{});
      }
    }
    return value{};
  }
};

namespace mini_detail {

/// Deeply nested input must not exhaust the stack. A recursive-descent parser
/// with no limit turns one small document into a crash, which fuzz_json_decode
/// reached after 2.37 million executions.
inline constexpr int max_parse_depth = 100;

struct reader {
  std::string_view text;
  std::size_t pos = 0;
  int depth = 0;

  void skip_space() {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
      ++pos;
    }
  }
  [[nodiscard]] auto peek() const -> char { return pos < text.size() ? text[pos] : '\0'; }
  [[nodiscard]] auto done() const -> bool { return pos >= text.size(); }
};

auto parse_value(reader& input) -> ce::result<mini_codec::value>;

inline auto hex_value(char character) noexcept -> int {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

/// \brief Append one code point as UTF-8.
/// \brief Append one code point as UTF-8.
inline void append_utf8(std::uint32_t code, std::string& out) {
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  }
}

inline auto parse_string_body(reader& input) -> ce::result<std::string> {
  if (input.peek() != '"') {
    return ce::fail(ce::errc::parse_error, "expected a string");
  }
  ++input.pos;
  std::string out;
  while (!input.done()) {
    const char character = input.text[input.pos++];
    if (character == '"') {
      return out;
    }
    if (character == '\\') {
      if (input.done()) {
        break;
      }
      const char escape = input.text[input.pos++];
      switch (escape) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (input.pos + 4 > input.text.size()) {
            return ce::fail(ce::errc::parse_error, "truncated \\u escape");
          }
          std::uint32_t code = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = input.text[input.pos++];
            code *= 16;
            if (digit >= '0' && digit <= '9') {
              code += static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              code += static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              code += static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
              return ce::fail(ce::errc::parse_error, "bad hex in \\u escape");
            }
          }
          // A character outside the BMP arrives as a surrogate PAIR, and the
          // Java SDK writes one for every emoji. Encoding each half on its own
          // produces CESU-8, which is not valid UTF-8; substituting a character
          // loses the data silently. Both were wrong here, and the interop
          // corpus is what found it.
          if (code >= 0xD800 && code <= 0xDBFF) {
            if (input.pos + 6 > input.text.size() || input.text[input.pos] != '\\' ||
                input.text[input.pos + 1] != 'u') {
              return ce::fail(ce::errc::parse_error, "high surrogate with no low surrogate");
            }
            input.pos += 2;
            std::uint32_t low = 0;
            for (int i = 0; i < 4; ++i) {
              const int digit = hex_value(input.text[input.pos++]);
              if (digit < 0) {
                return ce::fail(ce::errc::parse_error, "bad hex in \\u escape");
              }
              low = (low * 16) + static_cast<std::uint32_t>(digit);
            }
            if (low < 0xDC00 || low > 0xDFFF) {
              return ce::fail(ce::errc::parse_error, "high surrogate not followed by a low one");
            }
            code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            return ce::fail(ce::errc::parse_error, "low surrogate with no high surrogate");
          }
          append_utf8(code, out);
          break;
        }
        default: return ce::fail(ce::errc::parse_error, "unknown escape");
      }
      continue;
    }
    out.push_back(character);
  }
  return ce::fail(ce::errc::parse_error, "unterminated string");
}

/// Restores the depth on every exit path, including the error ones.
struct depth_guard {
  reader& input;
  explicit depth_guard(reader& r) : input{r} { ++input.depth; }
  ~depth_guard() { --input.depth; }
  depth_guard(const depth_guard&) = delete;
  auto operator=(const depth_guard&) -> depth_guard& = delete;
};

inline auto parse_number(reader& input) -> ce::result<mini_codec::value> {
  const std::size_t start = input.pos;
  if (input.peek() == '-') {
    ++input.pos;
  }
  bool fractional = false;
  while (!input.done()) {
    const char character = input.peek();
    if (character >= '0' && character <= '9') {
      ++input.pos;
    } else if (character == '.' || character == 'e' || character == 'E' || character == '+' ||
               character == '-') {
      fractional = fractional || character == '.' || character == 'e' || character == 'E';
      ++input.pos;
    } else {
      break;
    }
  }
  const std::string token{input.text.substr(start, input.pos - start)};
  if (token.empty() || token == "-") {
    return ce::fail(ce::errc::parse_error, "expected a number");
  }
  // stoll and stod throw, on a malformed token ("+") and on an out-of-range one
  // alike, and the SDK builds with exceptions disabled. Requiring the whole
  // token to be consumed is also what rejects "1.2.3", which the scan above
  // admits.
  if (fractional) {
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || errno == ERANGE) {
      return ce::fail(ce::errc::parse_error, "not a JSON number", token);
    }
    return mini_codec::make_double(parsed);
  }
  std::int64_t parsed = 0;
  const char* const first = token.data();
  const char* const last = first + token.size();
  const auto [stop, code] = std::from_chars(first, last, parsed);
  if (code != std::errc{} || stop != last) {
    return ce::fail(ce::errc::parse_error, "not a JSON integer", token);
  }
  return mini_codec::make_int(parsed);
}

inline auto parse_value(reader& input) -> ce::result<mini_codec::value> {
  if (input.depth >= max_parse_depth) {
    return ce::fail(ce::errc::parse_error, "JSON nested beyond the depth limit");
  }
  const depth_guard guard{input};
  input.skip_space();
  if (input.done()) {
    return ce::fail(ce::errc::parse_error, "unexpected end of input");
  }
  const char character = input.peek();

  if (character == '{') {
    ++input.pos;
    auto object = mini_codec::make_object();
    input.skip_space();
    if (input.peek() == '}') {
      ++input.pos;
      return object;
    }
    while (true) {
      input.skip_space();
      auto key = parse_string_body(input);
      if (!key) {
        return ce::fail(key.error().code, key.error().detail);
      }
      input.skip_space();
      if (input.peek() != ':') {
        return ce::fail(ce::errc::parse_error, "expected ':'");
      }
      ++input.pos;
      auto member = parse_value(input);
      if (!member) {
        return member;
      }
      // Duplicate keys are kept rather than merged, so the format layer decides.
      object.members.push_back(
          mini_codec::entry{.key = std::move(*key), .item = std::move(*member)});
      input.skip_space();
      if (input.peek() == ',') {
        ++input.pos;
        continue;
      }
      if (input.peek() == '}') {
        ++input.pos;
        return object;
      }
      return ce::fail(ce::errc::parse_error, "expected ',' or '}'");
    }
  }

  if (character == '[') {
    ++input.pos;
    auto array = mini_codec::make_array();
    input.skip_space();
    if (input.peek() == ']') {
      ++input.pos;
      return array;
    }
    while (true) {
      auto element = parse_value(input);
      if (!element) {
        return element;
      }
      array.elements.push_back(std::move(*element));
      input.skip_space();
      if (input.peek() == ',') {
        ++input.pos;
        continue;
      }
      if (input.peek() == ']') {
        ++input.pos;
        return array;
      }
      return ce::fail(ce::errc::parse_error, "expected ',' or ']'");
    }
  }

  if (character == '"') {
    auto text = parse_string_body(input);
    if (!text) {
      return ce::fail(text.error().code, text.error().detail);
    }
    return mini_codec::make_string(*text);
  }

  if (input.text.substr(input.pos).starts_with("true")) {
    input.pos += 4;
    return mini_codec::make_bool(true);
  }
  if (input.text.substr(input.pos).starts_with("false")) {
    input.pos += 5;
    return mini_codec::make_bool(false);
  }
  if (input.text.substr(input.pos).starts_with("null")) {
    input.pos += 4;
    return mini_codec::make_null();
  }
  return parse_number(input);
}

inline void dump_string(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char character : text) {
    switch (character) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20) {
          out += "\\u00";
          const char* digits = "0123456789abcdef";
          out.push_back(digits[(static_cast<unsigned char>(character) >> 4) & 0xF]);
          out.push_back(digits[static_cast<unsigned char>(character) & 0xF]);
        } else {
          out.push_back(character);
        }
    }
  }
  out.push_back('"');
}

inline void dump_value(std::string& out, const mini_codec::value& subject) {
  switch (subject.tag) {
    case ce::json::kind::null: out += "null"; break;
    case ce::json::kind::boolean: out += subject.boolean ? "true" : "false"; break;
    case ce::json::kind::integer: out += std::to_string(subject.integer); break;
    case ce::json::kind::floating: {
      std::string rendered = std::to_string(subject.number);
      while (rendered.size() > 1 && rendered.back() == '0' &&
             rendered.find('.') != std::string::npos) {
        rendered.pop_back();
      }
      if (!rendered.empty() && rendered.back() == '.') {
        rendered.pop_back();
      }
      out += rendered;
      break;
    }
    case ce::json::kind::string: dump_string(out, subject.text); break;
    case ce::json::kind::array: {
      out.push_back('[');
      bool first = true;
      for (const auto& element : subject.elements) {
        if (!first) { out.push_back(','); }
        first = false;
        dump_value(out, element);
      }
      out.push_back(']');
      break;
    }
    case ce::json::kind::object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, member] : subject.members) {
        if (!first) { out.push_back(','); }
        first = false;
        dump_string(out, key);
        out.push_back(':');
        dump_value(out, member);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace mini_detail

inline auto mini_codec::parse(std::string_view text) -> ce::result<value> {
  mini_detail::reader input{.text = text};
  auto parsed = mini_detail::parse_value(input);
  if (!parsed) {
    return parsed;
  }
  input.skip_space();
  if (!input.done()) {
    return ce::fail(ce::errc::parse_error, "trailing characters after the document");
  }
  return parsed;
}

inline auto mini_codec::dump(const value& subject) -> std::string {
  std::string out;
  mini_detail::dump_value(out, subject);
  return out;
}

/// Objects compare as unordered: same size, and every member of `left` finds an
/// equal value under its key in `right` (the first occurrence, as `find` does).
inline auto mini_codec::equal(const value& left, const value& right) -> bool {
  if (left.tag != right.tag) {
    return false;
  }
  switch (left.tag) {
    case ce::json::kind::null: return true;
    case ce::json::kind::boolean: return left.boolean == right.boolean;
    case ce::json::kind::integer: return left.integer == right.integer;
    case ce::json::kind::floating: return left.number == right.number;
    case ce::json::kind::string: return left.text == right.text;
    case ce::json::kind::array: return std::ranges::equal(left.elements, right.elements, &equal);
    case ce::json::kind::object: {
      if (left.members.size() != right.members.size()) {
        return false;
      }
      for (const auto& [key, member] : left.members) {
        const value* other = find(right, key);
        if (other == nullptr || !equal(member, *other)) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

static_assert(ce::json::json_codec<mini_codec>);

}  // namespace ce::test
