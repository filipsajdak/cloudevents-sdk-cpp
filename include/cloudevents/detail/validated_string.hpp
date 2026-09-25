#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/result.hpp>

namespace ce::v2::detail {

[[noreturn]] void this_literal_is_not_a_valid_cloudevents_attribute();

template <class Policy>
concept names_offending_text = requires { requires Policy::names_offending_text; };

template <class Policy>
class literal {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  consteval literal(const char* text) : text_{text} { reject_if_invalid(); }
  consteval literal(const char* text, std::size_t size) : text_{text, size} { reject_if_invalid(); }

  [[nodiscard]] constexpr auto view() const noexcept -> std::string_view { return text_; }

 private:
  consteval void reject_if_invalid() const {
    if (Policy::check(text_)) {
      this_literal_is_not_a_valid_cloudevents_attribute();
    }
  }

  std::string_view text_;  // NOLINT(scudoai-copy-view-member)
};

template <class Policy>
class validated_string {
 public:
  using policy_type = Policy;

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr validated_string(literal<Policy> proof) noexcept : borrowed_{proof.view()} {}

  [[nodiscard]] static auto make(std::string text) -> result<validated_string> {
    if (const auto refused = Policy::check(text); refused) {
      if constexpr (names_offending_text<Policy>) {
        return fail(*refused, std::move(text));
      } else {
        return fail(*refused);
      }
    }
    validated_string out;
    out.owned_ = std::move(text);
    out.borrowed_ = out.owned_;
    return out;
  }

  [[nodiscard]] static auto make(std::string_view text) -> result<validated_string> {
    return make(std::string{text});
  }

  validated_string(const validated_string& other)
      : owned_{other.owned_},
        borrowed_{other.owned_.empty() ? other.borrowed_ : std::string_view{owned_}} {}

  validated_string(validated_string&& other) noexcept
      : owned_{std::move(other.owned_)}, borrowed_{other.borrowed_} {
    if (!owned_.empty()) {
      borrowed_ = owned_;
    }
    other.borrowed_ = {};
  }

  auto operator=(const validated_string& other) -> validated_string& {
    if (this != &other) {
      owned_ = other.owned_;
      borrowed_ = owned_.empty() ? other.borrowed_ : std::string_view{owned_};
    }
    return *this;
  }

  auto operator=(validated_string&& other) noexcept -> validated_string& {
    if (this != &other) {
      owned_ = std::move(other.owned_);
      borrowed_ = owned_.empty() ? other.borrowed_ : std::string_view{owned_};
      other.borrowed_ = {};
    }
    return *this;
  }

  ~validated_string() = default;

  [[nodiscard]] constexpr auto view() const noexcept -> std::string_view { return borrowed_; }
  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return borrowed_.size(); }
  [[nodiscard]] auto str() const -> std::string { return std::string{borrowed_}; }

  [[nodiscard]] friend auto operator==(const validated_string& left,
                                       const validated_string& right) noexcept -> bool {
    return left.borrowed_ == right.borrowed_;
  }
  [[nodiscard]] friend auto operator<=>(const validated_string& left,
                                        const validated_string& right) noexcept
      -> std::strong_ordering {
    return left.borrowed_ <=> right.borrowed_;
  }

  [[nodiscard]] friend auto operator==(const validated_string& left,
                                       std::string_view right) noexcept -> bool {
    return left.borrowed_ == right;
  }
  [[nodiscard]] friend auto operator<=>(const validated_string& left,
                                        std::string_view right) noexcept -> std::strong_ordering {
    return left.borrowed_ <=> right;
  }

 private:
  validated_string() = default;

  std::string owned_;
  std::string_view borrowed_;  // NOLINT(scudoai-copy-view-member)
};

}  // namespace ce::v2::detail

// spec: SWR-BUILD-0005
namespace ce::inline v3::detail {
using ce::v2::detail::validated_string;
}  // namespace ce::inline v3::detail
