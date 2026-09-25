#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <ctre.hpp>

namespace ce::inline v3::json::detail {

// spec: SWR-JSON-0043
inline constexpr auto json_number_pattern =
    ctll::fixed_string{R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+\-]?[0-9]+)?)"};

inline constexpr std::string_view data_member_name = "data";
inline constexpr std::string_view true_literal = "true";
inline constexpr std::string_view false_literal = "false";
inline constexpr std::string_view null_literal = "null";
inline constexpr std::string_view json_whitespace = " \t\n\r";
inline constexpr std::string_view scalar_first_characters = "-0123456789tfn";
inline constexpr std::string_view single_character_escapes = "\"\\/bfnrt";
inline constexpr std::string_view hex_digits = "0123456789abcdefABCDEF";
inline constexpr std::size_t escape_introducer_length = 2;
inline constexpr std::size_t unicode_escape_digits = 4;
inline constexpr unsigned char first_unescaped_character = 0x20;

enum class char_class : std::uint8_t {
  other,
  whitespace,
  open,
  close,
  comma,
  colon,
  quote,
  scalar_start,
};

inline constexpr std::size_t character_count =
    std::size_t{std::numeric_limits<unsigned char>::max()} + 1;

inline constexpr auto char_classes = [] {
  std::array<char_class, character_count> table{};
  const auto mark = [&table](std::string_view members, char_class kind) {
    for (const char member : members) {
      table.at(static_cast<unsigned char>(member)) = kind;
    }
  };
  mark(json_whitespace, char_class::whitespace);
  mark("{[", char_class::open);
  mark("}]", char_class::close);
  mark(",", char_class::comma);
  mark(":", char_class::colon);
  mark("\"", char_class::quote);
  mark(scalar_first_characters, char_class::scalar_start);
  return table;
}();

[[nodiscard]] constexpr auto class_of(char character) noexcept -> char_class {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
  return char_classes[static_cast<unsigned char>(character)];
}

[[nodiscard]] constexpr auto ends_scalar(char character) noexcept -> bool {
  const auto kind = class_of(character);
  return kind == char_class::whitespace || kind == char_class::close || kind == char_class::comma;
}

[[nodiscard]] constexpr auto ends_string_run(char character) noexcept -> bool {
  return character == '"' || character == '\\' ||
         static_cast<unsigned char>(character) < first_unescaped_character;
}

using byte_word = std::uint64_t;
inline constexpr std::size_t word_bytes = sizeof(byte_word);
inline constexpr unsigned bits_per_byte = std::numeric_limits<unsigned char>::digits;
inline constexpr byte_word every_byte_one =
    std::numeric_limits<byte_word>::max() / std::numeric_limits<unsigned char>::max();
inline constexpr byte_word every_byte_high_bit = every_byte_one << (bits_per_byte - 1);

[[nodiscard]] constexpr auto bytes_below(byte_word bytes, unsigned char limit) noexcept
    -> byte_word {
  return (bytes - (every_byte_one * limit)) & ~bytes & every_byte_high_bit;
}

[[nodiscard]] constexpr auto bytes_equal(byte_word bytes, char wanted) noexcept -> byte_word {
  return bytes_below(bytes ^ (every_byte_one * static_cast<unsigned char>(wanted)), 1);
}

[[nodiscard]] constexpr auto string_run_ends(byte_word bytes) noexcept -> byte_word {
  return bytes_equal(bytes, '"') | bytes_equal(bytes, '\\') |
         bytes_below(bytes, first_unescaped_character);
}

template <class Text>
concept owning_temporary = std::same_as<Text, std::string>;

// spec: SWR-JSON-0043
class json_slicer {
 public:
  constexpr explicit json_slicer(std::string_view text) noexcept : text_{text} {}
  template <owning_temporary Text>
  explicit json_slicer(Text&& text) = delete;

  [[nodiscard]] constexpr auto lost() const noexcept -> bool { return lost_; }

  [[nodiscard]] constexpr auto at_end() noexcept -> bool {
    skip_space();
    return pos_ == text_.size();
  }

  [[nodiscard]] constexpr auto take(char expected) noexcept -> bool {
    skip_space();
    if (lost_ || peek() != expected) {
      return false;
    }
    ++pos_;
    return true;
  }

  [[nodiscard]] constexpr auto next_is(char expected) noexcept -> bool {
    skip_space();
    return !lost_ && peek() == expected;
  }

  constexpr void lose() noexcept { lost_ = true; }

  [[nodiscard]] constexpr auto event_data() noexcept -> std::optional<std::string_view> {
    skip_space();
    if (lost_) {
      return std::nullopt;
    }
    if (peek() != '{') {
      if (!value()) {
        lose();
      }
      return std::nullopt;
    }
    ++pos_;
    if (take('}')) {
      return std::nullopt;
    }
    std::optional<std::string_view> data{};
    bool certain = true;
    while (true) {
      const auto member = next_member();
      if (!member) {
        lose();
        return std::nullopt;
      }
      if (member->escaped_name) {
        certain = false;
      } else if (member->name == data_member_name) {
        certain = certain && !data.has_value();
        data = member->value;
      }
      if (take('}')) {
        break;
      }
      if (!take(',') || next_is('}')) {
        lose();
        return std::nullopt;
      }
    }
    return certain ? data : std::nullopt;
  }

 private:
  struct object_member {
    std::string_view name;   // NOLINT(scudoai-copy-view-member)
    std::string_view value;  // NOLINT(scudoai-copy-view-member)
    bool escaped_name;
  };

  std::string_view text_;  // NOLINT(scudoai-copy-view-member)
  std::size_t pos_ = 0;
  bool lost_ = false;

  [[nodiscard]] constexpr auto at(std::size_t index) const noexcept -> char {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return index < text_.size() ? text_[index] : '\0';
  }

  [[nodiscard]] constexpr auto peek(std::size_t ahead = 0) const noexcept -> char {
    return at(pos_ + ahead);
  }

  [[nodiscard]] static constexpr auto one_of(std::string_view set, char character) noexcept
      -> bool {
    return character != '\0' && set.find(character) != std::string_view::npos;
  }

  [[nodiscard]] constexpr auto span(std::size_t first, std::size_t last) const noexcept
      -> std::string_view {
    auto part = text_;
    part.remove_suffix(text_.size() - last);
    part.remove_prefix(first);
    return part;
  }

  constexpr void skip_space() noexcept {
    while (pos_ < text_.size() && class_of(peek()) == char_class::whitespace) {
      ++pos_;
    }
  }

  [[nodiscard]] constexpr auto string_run_end(std::size_t from) const noexcept -> std::size_t {
    if (!std::is_constant_evaluated()) {
      while (from + word_bytes <= text_.size()) {
        byte_word bytes{};
        std::memcpy(&bytes, span(from, from + word_bytes).data(), word_bytes);
        if (const auto ends = string_run_ends(bytes); ends != 0) {
          if constexpr (std::endian::native == std::endian::little) {
            return from + (static_cast<std::size_t>(std::countr_zero(ends)) / bits_per_byte);
          }
          break;
        }
        from += word_bytes;
      }
    }
    while (from < text_.size() && !ends_string_run(at(from))) {
      ++from;
    }
    return from;
  }

  [[nodiscard]] constexpr auto next_member() noexcept -> std::optional<object_member> {
    skip_space();
    const auto name_start = pos_;
    const auto escaped = string();
    if (!escaped) {
      return std::nullopt;
    }
    const auto name_end = pos_ - 1;
    const auto name = span(name_start + 1, name_end);
    if (!take(':')) {
      return std::nullopt;
    }
    skip_space();
    const auto value_start = pos_;
    if (!value()) {
      return std::nullopt;
    }
    return object_member{
        .name = name,
        .value = span(value_start, pos_),
        .escaped_name = *escaped,
    };
  }

  [[nodiscard]] constexpr auto string() noexcept -> std::optional<bool> {
    if (peek() != '"') {
      return std::nullopt;
    }
    ++pos_;
    bool escaped = false;
    while (true) {
      pos_ = string_run_end(pos_);
      const char stop = peek();
      if (stop == '"') {
        ++pos_;
        return escaped;
      }
      if (stop != '\\' || pos_ == text_.size()) {
        return std::nullopt;
      }
      escaped = true;
      if (!escape()) {
        return std::nullopt;
      }
    }
  }

  [[nodiscard]] constexpr auto escape() noexcept -> bool {
    const char kind = peek(1);
    if (one_of(single_character_escapes, kind)) {
      pos_ += escape_introducer_length;
      return true;
    }
    if (kind != 'u') {
      return false;
    }
    for (std::size_t digit = 0; digit < unicode_escape_digits; ++digit) {
      if (!one_of(hex_digits, peek(escape_introducer_length + digit))) {
        return false;
      }
    }
    pos_ += escape_introducer_length + unicode_escape_digits;
    return true;
  }

  [[nodiscard]] constexpr auto scalar() noexcept -> bool {
    const auto start = pos_;
    if (class_of(peek()) != char_class::scalar_start) {
      return false;
    }
    auto end = start + 1;
    while (end < text_.size() && !ends_scalar(at(end))) {
      ++end;
    }
    pos_ = end;
    const auto token = span(start, end);
    switch (token.front()) {
      case 't':
        return token == true_literal;
      case 'f':
        return token == false_literal;
      case 'n':
        return token == null_literal;
      default:
        return static_cast<bool>(ctre::match<json_number_pattern>(token));
    }
  }

  [[nodiscard]] constexpr auto value() noexcept -> bool {
    const char first = peek();
    if (first == '"') {
      return string().has_value();
    }
    if (first != '{' && first != '[') {
      return scalar();
    }
    std::size_t depth = 0;
    auto previous = char_class::open;
    while (pos_ < text_.size()) {
      const auto kind = class_of(at(pos_));
      switch (kind) {
        case char_class::whitespace:
          ++pos_;
          continue;
        case char_class::open:
          ++depth;
          ++pos_;
          break;
        case char_class::close:
          if (previous == char_class::comma) {
            return false;
          }
          --depth;
          ++pos_;
          if (depth == 0) {
            return true;
          }
          break;
        case char_class::comma:
        case char_class::colon:
          ++pos_;
          break;
        case char_class::quote:
          if (!string()) {
            return false;
          }
          break;
        case char_class::scalar_start:
          if (!scalar()) {
            return false;
          }
          break;
        case char_class::other:
          return false;
      }
      previous = kind;
    }
    return false;
  }
};

// spec: SWR-JSON-0043
[[nodiscard]] constexpr auto data_member_text(std::string_view event_text) noexcept
    -> std::optional<std::string_view> {
  json_slicer slicer{event_text};
  const auto data = slicer.event_data();
  if (slicer.lost() || !slicer.at_end()) {
    return std::nullopt;
  }
  return data;
}

template <owning_temporary Text>
auto data_member_text(Text&& event_text) -> std::optional<std::string_view> = delete;

// spec: SWR-JSON-0043
class batch_data_slices {
 public:
  template <owning_temporary Text>
  explicit batch_data_slices(Text&& batch_text) = delete;
  constexpr explicit batch_data_slices(std::string_view batch_text) noexcept : slicer_{batch_text} {
    if (!slicer_.take('[')) {
      slicer_.lose();
    }
  }

  [[nodiscard]] constexpr auto next() noexcept -> std::optional<std::string_view> {
    if (slicer_.lost()) {
      return std::nullopt;
    }
    if (!first_ && (!slicer_.take(',') || slicer_.next_is(']'))) {
      slicer_.lose();
      return std::nullopt;
    }
    first_ = false;
    return slicer_.event_data();
  }

 private:
  json_slicer slicer_;
  bool first_ = true;
};

}  // namespace ce::inline v3::json::detail
