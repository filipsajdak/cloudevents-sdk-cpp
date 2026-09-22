#pragma once

/// \file
/// \brief Attribute types that cannot hold a value the specification forbids.

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/result.hpp>

namespace ce::inline v1::detail {

/// \brief The diagnostic for a literal that broke its rule.
///
/// Declared and never defined. Calling a function with no definition inside a
/// constant expression is the error, and the function's name is the message.
/// A throw would be wrong: the no-exceptions preset forbids one, and this has to
/// work there (SWR-CORE-0027).
[[noreturn]] void this_literal_is_not_a_valid_cloudevents_attribute();

/// \brief Whether a refusal names the text that was offered rather than the
/// attribute it was offered for.
///
/// A context attribute is identified by which one it is, so an empty `id` reports
/// `id` (SWR-CORE-0017, SWR-CORE-0019). An extension has no fixed name to report:
/// the offered text is the only thing that identifies it (SWR-CORE-0020).
template <class Policy>
concept names_offending_text = requires { requires Policy::names_offending_text; };

/// \brief Proof that a compile-time constant passed its attribute's rule.
///
/// Only a consteval constructor can produce one, so a value of this type cannot
/// exist unless the rule accepted it. A runtime pointer or a runtime
/// `string_view` cannot reach these constructors: the call is an immediate
/// invocation, so a non-constant argument is ill-formed at the call site.
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

  std::string_view text_;
};

/// \brief A string that passed its attribute's rule, and cannot stop having done
/// so.
///
/// Storage is copy-on-write against static storage: `owned_` empty means the text
/// is borrowed from a string literal and cost nothing to hold. That invariant is
/// sound precisely because every policy refuses the empty string, so an owning
/// instance never holds one (SWR-CORE-0026).
template <class Policy>
class validated_string {
 public:
  using policy_type = Policy;

  /// The only implicit entry point, and it is safe: an invalid `literal` cannot
  /// be constructed, so this constructor is reachable only from a value the
  /// compiler already accepted.
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

  auto operator=(validated_string other) noexcept -> validated_string& {
    owned_.swap(other.owned_);
    borrowed_ = owned_.empty() ? other.borrowed_ : std::string_view{owned_};
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

  /// Heterogeneous, so a map keyed by this type still finds by `string_view`
  /// without building one of these to look up with.
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
  std::string_view borrowed_;
};

}  // namespace ce::inline v1::detail
