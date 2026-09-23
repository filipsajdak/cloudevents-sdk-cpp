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
#include <variant>
#include <vector>


#include <cloudevents/attributes.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-BUILD-0012
namespace ce::v2 {

using data_t = std::variant<std::monostate, std::string, binary, json_text>;

class event {
 public:
  using extension_map = std::map<extension_name, attribute_value, std::less<>>;

  struct options {
    std::optional<ce::v2::datacontenttype> datacontenttype = {};
    std::optional<ce::v2::dataschema> dataschema = {};
    std::optional<ce::v2::subject> subject = {};
    std::optional<timestamp> time = {};
    extension_map extensions = {};
    data_t data = {};

    friend auto operator==(const options&, const options&) -> bool = default;
  };

  explicit event(ce::v2::id identifier, ce::v2::source origin, ce::v2::type kind, options rest)
      : id_{std::move(identifier)},
        source_{std::move(origin)},
        type_{std::move(kind)},
        rest_{std::move(rest)} {}

  explicit event(ce::v2::id identifier, ce::v2::source origin, ce::v2::type kind)
      : event{std::move(identifier), std::move(origin), std::move(kind), options{}} {}

  struct builder {
    std::optional<ce::v2::id> id = {};
    std::optional<ce::v2::source> source = {};
    std::optional<ce::v2::type> type = {};
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

  [[nodiscard]] auto id() const noexcept -> const ce::v2::id& { return id_; }
  [[nodiscard]] auto source() const noexcept -> const ce::v2::source& { return source_; }
  [[nodiscard]] auto type() const noexcept -> const ce::v2::type& { return type_; }
  [[nodiscard]] static auto specversion() noexcept -> spec_version { return {}; }

  [[nodiscard]] auto datacontenttype() const noexcept -> const std::optional<ce::v2::datacontenttype>& {
    return rest_.datacontenttype;
  }
  [[nodiscard]] auto dataschema() const noexcept -> const std::optional<ce::v2::dataschema>& {
    return rest_.dataschema;
  }
  [[nodiscard]] auto subject() const noexcept -> const std::optional<ce::v2::subject>& {
    return rest_.subject;
  }
  [[nodiscard]] auto time() const noexcept -> const std::optional<timestamp>& { return rest_.time; }
  [[nodiscard]] auto extensions() const noexcept -> const extension_map& { return rest_.extensions; }
  [[nodiscard]] auto data() const noexcept -> const data_t& { return rest_.data; }

  void set_data(data_t payload, std::optional<ce::v2::datacontenttype> media_type) {
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
        mapping_error = fail(read.error().code, read.error().detail, read.error().where);
        return;
      }
      field = std::move(*read);
    });

    if (!mapping_error) {
      return fail(mapping_error.error().code, mapping_error.error().detail, mapping_error.error().where);
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
        mapping_error = fail(attribute.error().code, attribute.error().detail,
                             attribute.error().where);
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
  ce::v2::id id_;
  ce::v2::source source_;
  ce::v2::type type_;
  options rest_;
};

}  // namespace ce::v2
