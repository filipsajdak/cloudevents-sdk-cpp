#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace ce::v3::detail {

/// The one accessor both generations' documents and events grant access to, so
/// that include/cloudevents/v3_conversion.hpp can hand a model or a member over
/// without either generation widening its public surface (ADR-0012).
struct generation_access;

class json_document_model {
 public:
  json_document_model() = default;
  json_document_model(const json_document_model&) = delete;
  json_document_model(json_document_model&&) = delete;
  auto operator=(const json_document_model&) -> json_document_model& = delete;
  auto operator=(json_document_model&&) -> json_document_model& = delete;
  virtual ~json_document_model() = default;

  [[nodiscard]] virtual auto identity() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto dump() const -> std::string = 0;
  [[nodiscard]] virtual auto equal_value(const json_document_model& other) const -> bool = 0;
  [[nodiscard]] virtual auto equal_text(std::string_view text) const -> bool = 0;
};

template<class Codec>
class json_document_holder final : public json_document_model {
 public:
  explicit json_document_holder(Codec::value document) : value_(std::move(document)) {}

  [[nodiscard]] auto identity() const noexcept -> std::string_view override {
    return Codec::identity;
  }
  [[nodiscard]] auto dump() const -> std::string override { return Codec::dump(value_); }
  [[nodiscard]] auto equal_value(const json_document_model& other) const -> bool override {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return Codec::equal(value_, static_cast<const json_document_holder&>(other).value_);
  }
  [[nodiscard]] auto equal_text(std::string_view text) const -> bool override {
    const auto parsed = Codec::parse(text);
    return parsed.has_value() && Codec::equal(value_, *parsed);
  }
  [[nodiscard]] auto value() const noexcept -> const Codec::value& { return value_; }

 private:
  const Codec::value value_;
};

// spec: SWR-CORE-0035
class json_document_null_model final : public json_document_model {
 public:
  [[nodiscard]] auto identity() const noexcept -> std::string_view override { return {}; }
  [[nodiscard]] auto dump() const -> std::string override { return "null"; }
  [[nodiscard]] auto equal_value(const json_document_model& other) const -> bool override {
    return &other == this;
  }
  [[nodiscard]] auto equal_text([[maybe_unused]] std::string_view text) const -> bool override {
    return false;
  }
};

inline constexpr json_document_null_model moved_from_model{};

}  // namespace ce::v3::detail

// spec: SWR-BUILD-0005
namespace ce::inline v4::detail {
using ce::v3::detail::json_document_holder;
using ce::v3::detail::json_document_model;
using ce::v3::detail::json_document_null_model;
using ce::v3::detail::moved_from_model;
}  // namespace ce::inline v4::detail
