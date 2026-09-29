#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>


#include <cloudevents/attributes.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/json_document_model.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-BUILD-0013
namespace ce::v3 {

// spec: SWR-CORE-0036
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class json_document {
 public:
  template <json::json_codec Codec>
  [[nodiscard]] static auto make(Codec::value document) -> json_document {
    return json_document{
        std::make_shared<const detail::json_document_holder<Codec>>(std::move(document)),
    };
  }

  json_document(const json_document&) = default;
  auto operator=(const json_document&) -> json_document& = default;
  ~json_document() = default;

  template <json::json_codec Codec>
  [[nodiscard]] auto get() const noexcept -> const Codec::value* {
    if (!built_by<Codec>()) {
      return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return &static_cast<const detail::json_document_holder<Codec>&>(*model_).value();
  }

  template <json::json_codec Codec>
  [[nodiscard]] auto built_by() const noexcept -> bool {
    return model_->identity() == std::string_view{Codec::identity};
  }

  [[nodiscard]] auto dump() const -> std::string { return model_->dump(); }

  friend auto operator==(const json_document& left, const json_document& right) -> bool {
    if (left.model_ == right.model_) {
      return true;
    }
    if (left.model_->identity() == right.model_->identity()) {
      return left.model_->equal_value(*right.model_);
    }
    return left.model_->equal_text(right.model_->dump());
  }

 private:
  explicit json_document(std::shared_ptr<const detail::json_document_model> model)
      : model_{std::move(model)} {}

  std::shared_ptr<const detail::json_document_model> model_;
};

using data_t = std::variant<std::monostate, std::string, binary, json_text, json_document>;

class event {
 public:
  using extension_map = std::map<extension_name, attribute_value, std::less<>>;

  struct options {
    std::optional<ce::v3::datacontenttype> datacontenttype = {};
    std::optional<ce::v3::dataschema> dataschema = {};
    std::optional<ce::v3::subject> subject = {};
    std::optional<timestamp> time = {};
    extension_map extensions = {};
    data_t data = {};

    friend auto operator==(const options&, const options&) -> bool = default;
  };

  explicit event(ce::v3::id identifier, ce::v3::source origin, ce::v3::type kind, options rest)
      : id_{std::move(identifier)},
        source_{std::move(origin)},
        type_{std::move(kind)},
        rest_{std::move(rest)} {}

  explicit event(ce::v3::id identifier, ce::v3::source origin, ce::v3::type kind)
      : event{std::move(identifier), std::move(origin), std::move(kind), options{}} {}

  struct builder {
    std::optional<ce::v3::id> id = {};
    std::optional<ce::v3::source> source = {};
    std::optional<ce::v3::type> type = {};
    options rest = {};

    [[nodiscard]] auto build() && -> result<event> {
      if (!id) {
        return fail(errc::missing_required_attribute, "id must be present", "id");
      }
      if (!source) {
        return fail(errc::missing_required_attribute, "source must be present", "source");
      }
      if (!type) {
        return fail(errc::missing_required_attribute, "type must be present", "type");
      }
      return event{std::move(*id), std::move(*source), std::move(*type), std::move(rest)};
    }
  };

  [[nodiscard]] auto id() const noexcept -> const ce::v3::id& { return id_; }
  [[nodiscard]] auto source() const noexcept -> const ce::v3::source& { return source_; }
  [[nodiscard]] auto type() const noexcept -> const ce::v3::type& { return type_; }
  [[nodiscard]] static auto specversion() noexcept -> spec_version { return {}; }

  [[nodiscard]] auto datacontenttype() const noexcept -> const std::optional<ce::v3::datacontenttype>& {
    return rest_.datacontenttype;
  }
  [[nodiscard]] auto dataschema() const noexcept -> const std::optional<ce::v3::dataschema>& {
    return rest_.dataschema;
  }
  [[nodiscard]] auto subject() const noexcept -> const std::optional<ce::v3::subject>& {
    return rest_.subject;
  }
  [[nodiscard]] auto time() const noexcept -> const std::optional<timestamp>& { return rest_.time; }
  [[nodiscard]] auto extensions() const noexcept -> const extension_map& { return rest_.extensions; }
  [[nodiscard]] auto data() const noexcept -> const data_t& { return rest_.data; }

  void set_data(data_t payload, std::optional<ce::v3::datacontenttype> media_type) {
    rest_.data = std::move(payload);
    rest_.datacontenttype = std::move(media_type);
  }

  friend auto operator==(const event&, const event&) -> bool = default;

  void set_extension(extension_name name, attribute_value value) {
    rest_.extensions.insert_or_assign(std::move(name), std::move(value));
  }

  auto remove_extension(std::string_view name) -> bool {
    const auto found = rest_.extensions.find(name);
    if (found == rest_.extensions.end()) {
      return false;
    }
    rest_.extensions.erase(found);
    return true;
  }

  [[nodiscard]] auto extension(std::string_view name) const noexcept -> const attribute_value* {
    const auto found = rest_.extensions.find(name);
    return found == rest_.extensions.end() ? nullptr : &found->second;
  }

  template <described Ext>
  [[nodiscard]] auto get() const -> result<Ext> {
    static_assert(detail::extension_fields_supported<Ext>(),
                  "an extension struct may only declare bool, int32_t, std::string, uri, "
                  "uri_ref or timestamp fields, optionally wrapped in std::optional");

    Ext out{};
    result<void> mapping_error{};

    for_each_field(out, [this, &mapping_error](std::string_view name, auto& field) {
      using field_type = std::remove_cvref_t<decltype(field)>;
      if (!mapping_error) {
        return;
      }
      const attribute_value* stored = extension(name);
      if (stored == nullptr) {
        if constexpr (!detail::is_optional_field<field_type>) {
          mapping_error = fail(errc::missing_required_attribute,
                         "the extension requires this attribute", std::string{name});
        }
        return;
      }
      auto read = detail::read_attribute<field_type>(*stored, name);
      if (!read) {
        mapping_error = ce::v3::detail::forward_failure(std::move(read).error());
        return;
      }
      field = std::move(*read);
    });

    if (!mapping_error) {
      return ce::v3::detail::forward_failure(std::move(mapping_error).error());
    }
    return out;
  }

  template <described Ext>
  [[nodiscard]] auto set(const Ext& value) -> result<void> {
    static_assert(detail::extension_fields_supported<Ext>(),
                  "an extension struct may only declare bool, int32_t, std::string, uri, "
                  "uri_ref or timestamp fields, optionally wrapped in std::optional");

    result<void> mapping_error{};
    for_each_field(value, [this, &mapping_error](std::string_view name, const auto& field) {
      using field_type = std::remove_cvref_t<decltype(field)>;
      if (!mapping_error) {
        return;
      }
      if constexpr (detail::is_optional_field<field_type>) {
        if (!field) {
          static_cast<void>(remove_extension(name));
          return;
        }
      }
      auto attribute = extension_name::make(name);
      if (!attribute) {
        mapping_error = ce::v3::detail::forward_failure(std::move(attribute).error());
        return;
      }
      if constexpr (detail::is_optional_field<field_type>) {
        set_extension(std::move(*attribute), attribute_value{*field});
      } else {
        set_extension(std::move(*attribute), attribute_value{field});
      }
    });
    return mapping_error;
  }

  [[nodiscard]] auto lint() const -> std::vector<lint_warning> {
    std::vector<lint_warning> warnings;
    constexpr std::size_t recommended_name_length = 20;
    for (const auto& [name, value] : rest_.extensions) {
      if (name.size() > recommended_name_length) {
        warnings.push_back(lint_warning{
            .attribute = name.str(),
            .message = "extension names should be 20 characters or fewer",
        });
      }
    }
    return warnings;
  }

 private:
  ce::v3::id id_;
  ce::v3::source source_;
  ce::v3::type type_;
  options rest_;
};

}  // namespace ce::v3
