#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/detail/json_slice.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v3 {

namespace json {
// spec: SWR-JSON-0040
struct decode_options {
  static constexpr std::size_t default_retention_limit = std::size_t{16} * 1024;
  std::size_t retain_document_up_to = default_retention_limit;
};
}  // namespace json

// spec: SYS-JSON-0001
// spec: SWR-JSON-0010
template <json::json_codec Codec>
struct json_format {
  using value = Codec::value;

  // spec: SWR-JSON-0025
  static constexpr std::string_view content_type = json::content_type;
  static constexpr std::string_view batch_content_type = json::batch_content_type;

  [[nodiscard]] static auto to_value(const event& cloud_event) -> result<value> {
    auto root = Codec::make_object();
    Codec::set(root, "specversion", Codec::make_string(spec_version::view()));
    Codec::set(root, "id", Codec::make_string(cloud_event.id().view()));
    Codec::set(root, "source", Codec::make_string(cloud_event.source().view()));
    Codec::set(root, "type", Codec::make_string(cloud_event.type().view()));

    if (const auto& media_type = cloud_event.datacontenttype(); media_type) {
      Codec::set(root, "datacontenttype", Codec::make_string(media_type->view()));
    }
    if (const auto& schema = cloud_event.dataschema(); schema) {
      Codec::set(root, "dataschema", Codec::make_string(schema->view()));
    }
    if (const auto& named = cloud_event.subject(); named) {
      Codec::set(root, "subject", Codec::make_string(named->view()));
    }
    if (const auto& when = cloud_event.time(); when) {
      Codec::set(root, "time", Codec::make_string(to_string(*when)));
    }

    for (const auto& [name, attribute] : cloud_event.extensions()) {
      auto encoded = encode_attribute(attribute);
      if (!encoded) {
        return fail(encoded.error().code, encoded.error().detail, name.str());
      }
      Codec::set(root, name.view(), std::move(*encoded));
    }

    if (auto stored = encode_data(root, cloud_event.data()); !stored) {
      return fail(stored.error().code, stored.error().detail, stored.error().where);
    }

    return root;
  }

  [[nodiscard]] static auto encode(const event& cloud_event) -> result<std::string> {
    auto document = to_value(cloud_event);
    if (!document) {
      return fail(document.error().code, document.error().detail, document.error().where);
    }
    return Codec::dump(*document);
  }

  // spec: SWR-JSON-0026
  [[nodiscard]] static auto encode_batch(std::span<const event> events) -> result<std::string> {
    auto array = Codec::make_array();
    for (const auto& cloud_event : events) {
      auto document = to_value(cloud_event);
      if (!document) {
        return fail(document.error().code, document.error().detail, document.error().where);
      }
      Codec::push(array, std::move(*document));
    }
    return Codec::dump(array);
  }

  [[nodiscard]] static auto required_text(const value& document, std::string_view name)
      -> result<std::string_view> {
    return required_text_of(Codec::find(document, name), name);
  }

  [[nodiscard]] static auto optional_text(const value& document, std::string_view name)
      -> result<std::optional<std::string_view>> {
    return optional_text_of(Codec::find(document, name), name);
  }

  template <class Attribute>
  [[nodiscard]] static auto read_required(const value& document, std::string_view name,
                                          std::optional<Attribute>& slot) -> result<void> {
    return store_required(Codec::find(document, name), name, slot);
  }

  template <class Attribute>
  [[nodiscard]] static auto read_optional(const value& document, std::string_view name,
                                          std::optional<Attribute>& slot) -> result<void> {
    return store_optional(Codec::find(document, name), name, slot);
  }

  [[nodiscard]] static auto read_time(const value& document, event::options& into)
      -> result<void> {
    return store_time(Codec::find(document, "time"), into);
  }

  [[nodiscard]] static auto read_context_attributes(const value& document,
                                                    event::builder& into) -> result<void> {
    return read_context(
        members{
            .specversion = Codec::find(document, "specversion"),
            .id = Codec::find(document, "id"),
            .source = Codec::find(document, "source"),
            .type = Codec::find(document, "type"),
            .datacontenttype = Codec::find(document, "datacontenttype"),
            .dataschema = Codec::find(document, "dataschema"),
            .subject = Codec::find(document, "subject"),
            .time = Codec::find(document, "time"),
            .data = nullptr,
            .data_base64 = nullptr,
        },
        into);
  }

  // spec: SWR-JSON-0022
  // spec: SWR-JSON-0023
  // spec: SWR-JSON-0032
  [[nodiscard]] static auto read_extensions(const value& document, event::options& into)
      -> result<void> {
    result<void> outcome{};
    Codec::for_each_member(document, [&](std::string_view name, const value& member) {
      if (!outcome || reserved_name(name)) {
        return;
      }
      outcome = read_extension(name, member, into);
    });
    return outcome;
  }

  // spec: SWR-JSON-0031
  [[nodiscard]] static auto from_value(const value& document) -> result<event> {
    return read_event(document, nullptr, payload_mode::copy, std::nullopt);
  }

  // spec: SWR-JSON-0040
  // spec: SWR-JSON-0043
  [[nodiscard]] static auto decode(std::string_view text, json::decode_options options = {})
      -> result<event> {
    auto document = Codec::parse(text);
    if (!document) {
      return fail(document.error().code, document.error().detail);
    }
    if (retains(text, options)) {
      return read_event(*document, &*document, payload_mode::move, std::nullopt);
    }
    return read_event(*document, &*document, payload_mode::text,
                      json::detail::data_member_text(text));
  }

  // spec: SWR-JSON-0039
  // spec: SWR-JSON-0040
  // spec: SWR-JSON-0043
  [[nodiscard]] static auto decode_batch(std::string_view text, json::decode_options options = {})
      -> result<std::vector<event>> {
    auto document = Codec::parse(text);
    if (!document) {
      return fail(document.error().code, document.error().detail);
    }
    if (Codec::kind_of(*document) != json::kind::array) {
      return fail(errc::parse_error, "a batch must be a JSON array");
    }

    // The batch owns its parsed array, so each element's data member is moved
    // into its event, as decode moves the one member of a single event.
    const std::size_t event_count = Codec::size_of(*document);
    const auto mode =
        retains_batch(text, event_count, options) ? payload_mode::move : payload_mode::text;
    std::vector<event> events;
    events.reserve(event_count);
    result<void> element_error{};
    json::detail::batch_data_slices own_texts{text};
    Codec::for_each_mutable_element(*document, [&](value& element) {
      if (!element_error) {
        return;
      }
      auto cloud_event =
          read_event(element, &element, mode,
                     mode == payload_mode::text ? own_texts.next() : std::nullopt);
      if (!cloud_event) {
        element_error =
            fail(cloud_event.error().code, cloud_event.error().detail, cloud_event.error().where);
        return;
      }
      events.push_back(std::move(*cloud_event));
    });
    if (!element_error) {
      return fail(element_error.error().code, element_error.error().detail,
                  element_error.error().where);
    }
    return events;
  }

 private:
  enum class payload_mode : std::uint8_t { text, copy, move };

  [[nodiscard]] static auto retains(std::string_view text, const json::decode_options& options)
      -> bool {
    return options.retain_document_up_to != 0 && text.size() <= options.retain_document_up_to;
  }

  // spec: SWR-JSON-0040
  [[nodiscard]] static auto retains_batch(std::string_view text,
                                          std::size_t event_count,
                                          const json::decode_options& options) -> bool {
    const auto limit = options.retain_document_up_to;
    if (limit == 0) {
      return false;
    }
    return event_count > std::numeric_limits<std::size_t>::max() / limit ||
           text.size() <= limit * event_count;
  }

  [[nodiscard]] static auto read_event(const value& document, value* owned, payload_mode mode,
                                       std::optional<std::string_view> own_text)
      -> result<event> {
    if (Codec::kind_of(document) != json::kind::object) {
      return fail(errc::parse_error, "a CloudEvent must be a JSON object");
    }

    event::builder under_construction{};
    members found{};
    result<void> extensions_read{};
    Codec::for_each_member(document, [&](std::string_view name, const value& member) {
      if (claim(found, name, member) || !extensions_read) {
        return;
      }
      extensions_read = read_extension(name, member, under_construction.rest);
    });

    if (auto read = read_context(found, under_construction); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto stored = decode_data(found, owned, mode, own_text, under_construction.rest);
        !stored) {
      return fail(stored.error().code, stored.error().detail, stored.error().where);
    }
    if (!extensions_read) {
      return fail(extensions_read.error().code, extensions_read.error().detail,
                  extensions_read.error().where);
    }
    return std::move(under_construction).build();
  }

  struct members {
    const value* specversion{};
    const value* id{};
    const value* source{};
    const value* type{};
    const value* datacontenttype{};
    const value* dataschema{};
    const value* subject{};
    const value* time{};
    const value* data{};
    const value* data_base64{};
  };

  using member_slot = std::pair<std::string_view, const value* members::*>;

  static constexpr auto member_slots = std::to_array<member_slot>({
      {"specversion", &members::specversion},
      {"id", &members::id},
      {"source", &members::source},
      {"type", &members::type},
      {"datacontenttype", &members::datacontenttype},
      {"dataschema", &members::dataschema},
      {"subject", &members::subject},
      {"time", &members::time},
      {"data", &members::data},
      {"data_base64", &members::data_base64},
  });

  static_assert(member_slots.size() == detail::reserved_names.size());
  static_assert(std::ranges::all_of(detail::reserved_names, [](std::string_view name) {
    return std::ranges::find(member_slots, name, &member_slot::first) != member_slots.end();
  }));

  [[nodiscard]] static auto claim(members& found, std::string_view name, const value& member)
      -> bool {
    for (const auto& [slot_name, slot] : member_slots) {
      if (name == slot_name) {
        if (found.*slot == nullptr) {
          found.*slot = &member;
        }
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static auto required_text_of(const value* member, std::string_view name)
      -> result<std::string_view> {
    if (member == nullptr) {
      return fail(errc::missing_required_attribute, "required attribute is absent",
                  std::string{name});
    }
    auto text = Codec::as_string(*member);
    if (!text) {
      return fail(errc::invalid_attribute_value, "must be a JSON string", std::string{name});
    }
    return *text;
  }

  [[nodiscard]] static auto optional_text_of(const value* member, std::string_view name)
      -> result<std::optional<std::string_view>> {
    if (member == nullptr || Codec::kind_of(*member) == json::kind::null) {
      return std::optional<std::string_view>{};
    }
    auto text = Codec::as_string(*member);
    if (!text) {
      return fail(errc::invalid_attribute_value, "must be a JSON string", std::string{name});
    }
    return std::optional<std::string_view>{*text};
  }

  template <class Attribute>
  [[nodiscard]] static auto store_required(const value* member, std::string_view name,
                                           std::optional<Attribute>& slot) -> result<void> {
    const auto text = required_text_of(member, name);
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    return detail::store_attribute(slot, *text);
  }

  template <class Attribute>
  [[nodiscard]] static auto store_optional(const value* member, std::string_view name,
                                           std::optional<Attribute>& slot) -> result<void> {
    const auto text = optional_text_of(member, name);
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    if (const auto& present = *text; present) {
      return detail::store_attribute(slot, *present);
    }
    return {};
  }

  [[nodiscard]] static auto store_time(const value* member, event::options& into)
      -> result<void> {
    const auto text = optional_text_of(member, "time");
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    if (const auto& present = *text; present) {
      auto parsed = parse_timestamp(*present);
      if (!parsed) {
        return fail(parsed.error().code, parsed.error().detail, "time");
      }
      into.time = *parsed;
    }
    return {};
  }

  [[nodiscard]] static auto read_context(const members& found, event::builder& into)
      -> result<void> {
    const auto version = required_text_of(found.specversion, "specversion");
    if (!version) {
      return fail(version.error().code, version.error().detail, version.error().where);
    }
    if (auto supported = spec_version::make(*version); !supported) {
      return fail(supported.error().code, supported.error().detail, supported.error().where);
    }
    if (auto read = store_required(found.id, "id", into.id); !read) {
      return read;
    }
    if (auto read = store_required(found.source, "source", into.source); !read) {
      return read;
    }
    if (auto read = store_required(found.type, "type", into.type); !read) {
      return read;
    }
    if (auto read = store_optional(found.datacontenttype, "datacontenttype",
                                   into.rest.datacontenttype);
        !read) {
      return read;
    }
    if (auto read = store_optional(found.dataschema, "dataschema", into.rest.dataschema); !read) {
      return read;
    }
    if (auto read = store_optional(found.subject, "subject", into.rest.subject); !read) {
      return read;
    }
    return store_time(found.time, into.rest);
  }

  // spec: SWR-JSON-0022
  // spec: SWR-JSON-0023
  // spec: SWR-JSON-0032
  [[nodiscard]] static auto read_extension(std::string_view name, const value& member,
                                           event::options& into) -> result<void> {
    if (Codec::kind_of(member) == json::kind::null) {
      return {};
    }
    auto attribute = extension_name::make(name);
    if (!attribute) {
      return fail(attribute.error().code, attribute.error().detail, std::string{name});
    }
    auto decoded = decode_attribute(member);
    if (!decoded) {
      return fail(decoded.error().code, decoded.error().detail, std::string{name});
    }
    into.extensions.insert_or_assign(std::move(*attribute), std::move(*decoded));
    return {};
  }

  // spec: SWR-JSON-0011
  // spec: SWR-JSON-0013
  // spec: SWR-JSON-0014
  [[nodiscard]] static auto encode_attribute(const attribute_value& attribute) -> result<value> {
    return std::visit(
        [](const auto& held) -> result<value> {
          using held_type = std::remove_cvref_t<decltype(held)>;
          if constexpr (std::is_same_v<held_type, bool>) {
            return Codec::make_bool(held);
          } else if constexpr (std::is_same_v<held_type, std::int32_t>) {
            return Codec::make_int(held);
          } else if constexpr (std::is_same_v<held_type, std::string>) {
            return Codec::make_string(held);
          } else if constexpr (std::is_same_v<held_type, binary>) {
            return Codec::make_string(base64_encode(held));
          } else if constexpr (std::is_same_v<held_type, timestamp>) {
            return Codec::make_string(to_string(held));
          } else {
            return Codec::make_string(held.view());
          }
        },
        attribute);
  }

  // spec: SWR-JSON-0012
  // spec: SWR-JSON-0024
  // spec: SWR-JSON-0035
  [[nodiscard]] static auto decode_attribute(const value& member) -> result<attribute_value> {
    switch (Codec::kind_of(member)) {
      case json::kind::boolean: {
        auto held = Codec::as_bool(member);
        if (!held) {
          return fail(held.error().code, held.error().detail);
        }
        return attribute_value{*held};
      }
      case json::kind::integer: {
        auto held = Codec::as_int(member);
        if (!held) {
          return fail(held.error().code, held.error().detail);
        }
        if (*held < std::numeric_limits<std::int32_t>::min() ||
            *held > std::numeric_limits<std::int32_t>::max()) {
          return fail(errc::out_of_range, "the CloudEvents Integer type is 32-bit signed");
        }
        return attribute_value{static_cast<std::int32_t>(*held)};
      }
      case json::kind::string: {
        auto held = Codec::as_string(member);
        if (!held) {
          return fail(held.error().code, held.error().detail);
        }
        return attribute_value{std::string{*held}};
      }
      case json::kind::floating:
        return fail(errc::type_mismatch, "the CloudEvents type system has no floating-point type");
      case json::kind::null:
      case json::kind::array:
      case json::kind::object:
        break;
    }
    return fail(errc::type_mismatch, "an extension attribute must be a boolean, integer or string");
  }

  // spec: SWR-JSON-0015
  // spec: SWR-JSON-0016
  // spec: SWR-JSON-0017
  [[nodiscard]] static auto encode_data(value& root, const data_t& data) -> result<void> {
    return std::visit(
        [&root](const auto& held) -> result<void> {
          using held_type = std::remove_cvref_t<decltype(held)>;
          if constexpr (std::is_same_v<held_type, std::monostate>) {
            return {};
          } else if constexpr (std::is_same_v<held_type, binary>) {
            Codec::set(root, "data_base64", Codec::make_string(base64_encode(held)));
            return {};
          } else if constexpr (std::is_same_v<held_type, std::string>) {
            Codec::set(root, "data", Codec::make_string(held));
            return {};
          } else if constexpr (std::is_same_v<held_type, json_document>) {
            // spec: SWR-JSON-0041
            if (const auto* own = held.template get<Codec>(); own != nullptr) {
              Codec::set(root, "data", Codec::copy(*own));
              return {};
            }
            // spec: SWR-JSON-0042
            auto converted = Codec::parse(held.dump());
            if (!converted) {
              return fail(errc::parse_error, "data is not well-formed JSON", "/data");
            }
            Codec::set(root, "data", std::move(*converted));
            return {};
          } else {
            auto parsed = Codec::parse(held.raw);
            if (!parsed) {
              return fail(errc::parse_error, "data is not well-formed JSON", "/data");
            }
            Codec::set(root, "data", std::move(*parsed));
            return {};
          }
        },
        data);
  }

  // spec: SWR-JSON-0018
  // spec: SWR-JSON-0019
  // spec: SWR-JSON-0020
  // spec: SWR-JSON-0021
  [[nodiscard]] static auto decode_data(const members& found, value* owned, payload_mode mode,
                                        std::optional<std::string_view> own_text,
                                        event::options& into) -> result<void> {
    const value* data = found.data;
    const value* data_base64 = found.data_base64;
    if (data != nullptr && data_base64 != nullptr) {
      return fail(errc::data_conflict, "data and data_base64 are mutually exclusive", "data");
    }

    if (data_base64 != nullptr) {
      auto text = Codec::as_string(*data_base64);
      if (!text) {
        return fail(errc::invalid_attribute_value, "data_base64 must be a JSON string",
                    "data_base64");
      }
      auto decoded = base64_decode(*text);
      if (!decoded) {
        return fail(decoded.error().code, decoded.error().detail, "data_base64");
      }
      into.data = std::move(*decoded);
      return {};
    }

    if (data == nullptr) {
      return {};
    }

    const auto& media_type = into.datacontenttype;
    const bool declared_non_json = media_type && !is_json_content_type(media_type->view());
    if (declared_non_json && Codec::kind_of(*data) == json::kind::string) {
      auto text = Codec::as_string(*data);
      if (!text) {
        return fail(errc::invalid_attribute_value, "data must be a JSON string", "data");
      }
      into.data = std::string{*text};
      return {};
    }

    into.data = json_payload(*data, owned, mode, own_text);
    return {};
  }

  // spec: SWR-JSON-0019
  // spec: SWR-JSON-0040
  // spec: SWR-JSON-0043
  [[nodiscard]] static auto json_payload(const value& data, value* owned, payload_mode mode,
                                         std::optional<std::string_view> own_text) -> data_t {
    switch (mode) {
      case payload_mode::text:
        return json_text{.raw = own_text ? std::string{*own_text} : Codec::dump(data)};
      case payload_mode::copy: return json_document::make<Codec>(Codec::copy(data));
      case payload_mode::move: break;
    }
    if (owned == nullptr || Codec::find(*owned, "data") != &data) {
      return json_document::make<Codec>(Codec::copy(data));
    }
    return json_document::make<Codec>(Codec::extract(*owned, "data"));
  }
};

}  // namespace ce::inline v3
