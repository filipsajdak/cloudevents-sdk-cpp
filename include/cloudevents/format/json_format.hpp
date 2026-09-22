#pragma once

/// \file
/// \brief The CloudEvents JSON event format, over any `json_codec`.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1 {

/// \brief Encodes and decodes events in the JSON format.
template <json::json_codec Codec>
struct json_format {
  using value = Codec::value;

  static constexpr std::string_view content_type = json::content_type;
  static constexpr std::string_view batch_content_type = json::batch_content_type;

  // --- encode --------------------------------------------------------------

  /// \brief Build the JSON document for one event.
  [[nodiscard]] static auto to_value(const event& cloud_event) -> result<value> {
    auto root = Codec::make_object();
    Codec::set(root, "specversion", Codec::make_string(spec_version::view()));
    Codec::set(root, "id", Codec::make_string(cloud_event.id().view()));
    Codec::set(root, "source", Codec::make_string(cloud_event.source().view()));
    Codec::set(root, "type", Codec::make_string(cloud_event.type().view()));

    // Bound once rather than called twice: nothing says the second call yields
    // the engaged optional the first one tested.
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

  /// \brief Serialize one event.
  [[nodiscard]] static auto encode(const event& cloud_event) -> result<std::string> {
    auto document = to_value(cloud_event);
    if (!document) {
      return fail(document.error().code, document.error().detail, document.error().where);
    }
    return Codec::dump(*document);
  }

  /// \brief Serialize a batch. An empty batch is a valid empty array.
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

  // --- decode helpers ------------------------------------------------------

  /// A required member's string value, or a failure naming the member.
  [[nodiscard]] static auto required_text(const value& document, std::string_view name)
      -> result<std::string_view> {
    const auto* member = Codec::find(document, name);
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

  /// An optional member's string value. Absent and null both read as absent
  /// (JSON format section 2.2).
  [[nodiscard]] static auto optional_text(const value& document, std::string_view name)
      -> result<std::optional<std::string_view>> {
    const auto* member = Codec::find(document, name);
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
  [[nodiscard]] static auto read_required(const value& document, std::string_view name,
                                          std::optional<Attribute>& slot) -> result<void> {
    const auto text = required_text(document, name);
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    return detail::store_attribute(slot, *text);
  }

  template <class Attribute>
  [[nodiscard]] static auto read_optional(const value& document, std::string_view name,
                                          std::optional<Attribute>& slot) -> result<void> {
    const auto text = optional_text(document, name);
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    if (const auto& present = *text; present) {
      return detail::store_attribute(slot, *present);
    }
    return {};
  }

  [[nodiscard]] static auto read_time(const value& document, event::options& into)
      -> result<void> {
    const auto text = optional_text(document, "time");
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

  /// Every context attribute, each through its own factory, in the order the
  /// specification lists them.
  [[nodiscard]] static auto read_context_attributes(const value& document,
                                                    event::builder& into) -> result<void> {
    const auto version = required_text(document, "specversion");
    if (!version) {
      return fail(version.error().code, version.error().detail, version.error().where);
    }
    if (auto supported = spec_version::make(*version); !supported) {
      return fail(supported.error().code, supported.error().detail, supported.error().where);
    }
    if (auto read = read_required(document, "id", into.id); !read) {
      return read;
    }
    if (auto read = read_required(document, "source", into.source); !read) {
      return read;
    }
    if (auto read = read_required(document, "type", into.type); !read) {
      return read;
    }
    if (auto read = read_optional(document, "datacontenttype", into.rest.datacontenttype); !read) {
      return read;
    }
    if (auto read = read_optional(document, "dataschema", into.rest.dataschema); !read) {
      return read;
    }
    if (auto read = read_optional(document, "subject", into.rest.subject); !read) {
      return read;
    }
    return read_time(document, into.rest);
  }

  /// Anything not a context attribute is an extension. JSON has no room for the
  /// CloudEvents attribute type, so the type is inferred and documented as
  /// lossy; the typed extension structs recover it.
  [[nodiscard]] static auto read_extensions(const value& document, event::options& into)
      -> result<void> {
    result<void> outcome{};
    Codec::for_each_member(document, [&](std::string_view name, const value& member) {
      if (!outcome || reserved_name(name)) {
        return;
      }
      // JSON format section 2.2: an attribute encoded as null MUST be treated as
      // unset, and the format's own example carries an "unsetextension": null.
      if (Codec::kind_of(member) == json::kind::null) {
        return;
      }
      // An unknown member becomes an extension only if its name is one the spec
      // allows, or decode would return an event the encoder then refuses.
      auto attribute = extension_name::make(name);
      if (!attribute) {
        outcome = fail(attribute.error().code, attribute.error().detail, std::string{name});
        return;
      }
      auto decoded = decode_attribute(member);
      if (!decoded) {
        outcome = fail(decoded.error().code, decoded.error().detail, std::string{name});
        return;
      }
      into.extensions.insert_or_assign(std::move(*attribute), std::move(*decoded));
    });
    return outcome;
  }

  // --- decode --------------------------------------------------------------

  /// \brief Read one event from a JSON document.
  ///
  /// The context attributes, then the payload, then the extensions: the order
  /// decides which problem a document with several is reported for, and the
  /// version comes first so a document from another version is named as such.
  [[nodiscard]] static auto from_value(const value& document) -> result<event> {
    if (Codec::kind_of(document) != json::kind::object) {
      return fail(errc::parse_error, "a CloudEvent must be a JSON object");
    }

    event::builder under_construction{};
    if (auto read = read_context_attributes(document, under_construction); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto stored = decode_data(document, under_construction.rest); !stored) {
      return fail(stored.error().code, stored.error().detail, stored.error().where);
    }
    if (auto read = read_extensions(document, under_construction.rest); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    return std::move(under_construction).build();
  }

  /// \brief Read one event from JSON text.
  [[nodiscard]] static auto decode(std::string_view text) -> result<event> {
    auto document = Codec::parse(text);
    if (!document) {
      return fail(document.error().code, document.error().detail);
    }
    return from_value(*document);
  }

  /// \brief Read a batch. `[]` is a valid empty batch.
  [[nodiscard]] static auto decode_batch(std::string_view text) -> result<std::vector<event>> {
    auto document = Codec::parse(text);
    if (!document) {
      return fail(document.error().code, document.error().detail);
    }
    if (Codec::kind_of(*document) != json::kind::array) {
      return fail(errc::parse_error, "a batch must be a JSON array");
    }

    std::vector<event> events;
    result<void> element_error{};
    Codec::for_each_element(*document, [&](const value& element) {
      if (!element_error) {
        return;
      }
      auto cloud_event = from_value(element);
      if (!cloud_event) {
        element_error = fail(cloud_event.error().code, cloud_event.error().detail, cloud_event.error().where);
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
            // uri and uri_ref are textual on the wire; only the declared type
            // distinguishes them, and JSON cannot carry it.
            return Codec::make_string(held.view());
          }
        },
        attribute);
  }

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
        // The CloudEvents type system has no floating-point attribute, and
        // rounding would change the value silently (SPEC section 9, D6).
        return fail(errc::type_mismatch, "the CloudEvents type system has no floating-point type");
      case json::kind::null:
      case json::kind::array:
      case json::kind::object:
        break;
    }
    return fail(errc::type_mismatch, "an extension attribute must be a boolean, integer or string");
  }

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
          } else {
            // Pre-serialized JSON is parsed and spliced, so the output is one
            // well-formed document rather than an escaped blob. This is the only
            // place the SDK validates a json_text.
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

  [[nodiscard]] static auto decode_data(const value& document, event::options& into)
      -> result<void> {
    const auto* data = Codec::find(document, "data");
    const auto* data_base64 = Codec::find(document, "data_base64");

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

    // A JSON string under `data` is the payload itself when the content type says
    // it is not JSON. Otherwise the member is carried through as JSON.
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

    into.data = json_text{.raw = Codec::dump(*data)};
    return {};
  }
};

}  // namespace ce::inline v1
