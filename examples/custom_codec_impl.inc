// Ordinary JSON serialisation for the example codec. Kept out of the example's
// main narrative because none of it is about the SDK.

#include <cctype>

namespace demo {
namespace {

void dump_into(const value& v, std::string& out);

void dump_string(std::string_view text, std::string& out) {
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
          char buffer[7];
          std::snprintf(buffer, sizeof buffer, "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(character)));
          out += buffer;
        } else {
          out.push_back(character);
        }
    }
  }
  out.push_back('"');
}

void dump_into(const value& v, std::string& out) {
  switch (v.tag) {
    case ce::json::kind::null: out += "null"; break;
    case ce::json::kind::boolean: out += v.boolean ? "true" : "false"; break;
    case ce::json::kind::integer: out += std::to_string(v.integer); break;
    case ce::json::kind::floating: {
      char buffer[32];
      std::snprintf(buffer, sizeof buffer, "%.17g", v.number);
      out += buffer;
      break;
    }
    case ce::json::kind::string: dump_string(v.text, out); break;
    case ce::json::kind::array: {
      out.push_back('[');
      bool first = true;
      for (const auto& element : v.elements) {
        if (!first) { out.push_back(','); }
        first = false;
        dump_into(element, out);
      }
      out.push_back(']');
      break;
    }
    case ce::json::kind::object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, stored] : v.members) {
        if (!first) { out.push_back(','); }
        first = false;
        dump_string(key, out);
        out.push_back(':');
        dump_into(stored, out);
      }
      out.push_back('}');
      break;
    }
  }
}

struct reader {
  std::string_view text;
  std::size_t pos = 0;

  void skip_space() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' ||
                                 text[pos] == '\t')) {
      ++pos;
    }
  }
  [[nodiscard]] auto done() const -> bool { return pos >= text.size(); }
  [[nodiscard]] auto peek() const -> char { return pos < text.size() ? text[pos] : '\0'; }
};

auto parse_value(reader& in) -> ce::result<value>;

auto parse_string_body(reader& in) -> ce::result<std::string> {
  if (in.peek() != '"') {
    return ce::fail(ce::errc::parse_error, "expected a string");
  }
  ++in.pos;
  std::string out;
  while (!in.done()) {
    const char character = in.text[in.pos++];
    if (character == '"') {
      return out;
    }
    if (character != '\\') {
      out.push_back(character);
      continue;
    }
    if (in.done()) {
      break;
    }
    switch (in.text[in.pos++]) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'u': {
        if (in.pos + 4 > in.text.size()) {
          return ce::fail(ce::errc::parse_error, "truncated \\u escape");
        }
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
          const char digit = in.text[in.pos++];
          code <<= 4U;
          if (digit >= '0' && digit <= '9') { code |= static_cast<unsigned>(digit - '0'); }
          else if (digit >= 'a' && digit <= 'f') { code |= static_cast<unsigned>(digit - 'a' + 10); }
          else if (digit >= 'A' && digit <= 'F') { code |= static_cast<unsigned>(digit - 'A' + 10); }
          else { return ce::fail(ce::errc::parse_error, "bad \\u escape"); }
        }
        if (code < 0x80) {
          out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
          out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
          out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
          out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
          out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
          out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
        break;
      }
      default: return ce::fail(ce::errc::parse_error, "unknown escape");
    }
  }
  return ce::fail(ce::errc::parse_error, "unterminated string");
}

auto parse_number(reader& in) -> ce::result<value> {
  const std::size_t start = in.pos;
  bool fractional = false;
  if (in.peek() == '-' || in.peek() == '+') { ++in.pos; }
  while (!in.done()) {
    const char character = in.peek();
    if (character >= '0' && character <= '9') { ++in.pos; }
    else if (character == '.' || character == 'e' || character == 'E' || character == '+' ||
             character == '-') {
      fractional = fractional || character == '.' || character == 'e' || character == 'E';
      ++in.pos;
    } else { break; }
  }
  const std::string token{in.text.substr(start, in.pos - start)};
  if (token.empty()) {
    return ce::fail(ce::errc::parse_error, "expected a number");
  }
  errno = 0;
  char* end = nullptr;
  if (fractional) {
    const double parsed = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || errno == ERANGE) {
      return ce::fail(ce::errc::parse_error, "not a JSON number");
    }
    return codec::make_double(parsed);
  }
  const long long parsed = std::strtoll(token.c_str(), &end, 10);
  if (end != token.c_str() + token.size() || errno == ERANGE) {
    return ce::fail(ce::errc::parse_error, "not a JSON integer");
  }
  return codec::make_int(static_cast<std::int64_t>(parsed));
}

auto parse_value(reader& in) -> ce::result<value> {
  in.skip_space();
  if (in.done()) {
    return ce::fail(ce::errc::parse_error, "unexpected end of input");
  }
  const char character = in.peek();

  if (character == '{') {
    ++in.pos;
    auto object = codec::make_object();
    in.skip_space();
    if (in.peek() == '}') { ++in.pos; return object; }
    while (true) {
      in.skip_space();
      auto key = parse_string_body(in);
      if (!key) { return ce::fail(key.error().code, key.error().detail); }
      in.skip_space();
      if (in.peek() != ':') { return ce::fail(ce::errc::parse_error, "expected ':'"); }
      ++in.pos;
      auto member = parse_value(in);
      if (!member) { return member; }
      codec::set(object, *key, std::move(*member));
      in.skip_space();
      if (in.peek() == ',') { ++in.pos; continue; }
      if (in.peek() == '}') { ++in.pos; return object; }
      return ce::fail(ce::errc::parse_error, "expected ',' or '}'");
    }
  }
  if (character == '[') {
    ++in.pos;
    auto array = codec::make_array();
    in.skip_space();
    if (in.peek() == ']') { ++in.pos; return array; }
    while (true) {
      auto element = parse_value(in);
      if (!element) { return element; }
      codec::push(array, std::move(*element));
      in.skip_space();
      if (in.peek() == ',') { ++in.pos; continue; }
      if (in.peek() == ']') { ++in.pos; return array; }
      return ce::fail(ce::errc::parse_error, "expected ',' or ']'");
    }
  }
  if (character == '"') {
    auto text = parse_string_body(in);
    if (!text) { return ce::fail(text.error().code, text.error().detail); }
    return codec::make_string(*text);
  }
  if (in.text.substr(in.pos).starts_with("true")) { in.pos += 4; return codec::make_bool(true); }
  if (in.text.substr(in.pos).starts_with("false")) { in.pos += 5; return codec::make_bool(false); }
  if (in.text.substr(in.pos).starts_with("null")) { in.pos += 4; return codec::make_null(); }
  return parse_number(in);
}

}  // namespace

auto codec::dump(const value& v) -> std::string {
  std::string out;
  dump_into(v, out);
  return out;
}

auto codec::parse(std::string_view text) -> ce::result<value> {
  reader in{.text = text};
  auto parsed = parse_value(in);
  if (!parsed) { return parsed; }
  in.skip_space();
  if (!in.done()) {
    return ce::fail(ce::errc::parse_error, "trailing content after the JSON value");
  }
  return parsed;
}

}  // namespace demo
