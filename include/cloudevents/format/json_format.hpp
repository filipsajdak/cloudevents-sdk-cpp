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
  using value = typename Codec::value;

  static constexpr std::string_view content_type = json::content_type;
  static constexpr std::string_view batch_content_type = json::batch_content_type;

  // --- encode --------------------------------------------------------------

  /// \brief Build the JSON document for one event.
  [[nodiscard]] static auto to_value(const event& cloud_event) -> result<value> {
    if (auto valid = cloud_event.validate(); !valid) {
      return fail(valid.error().code, valid.error().detail, valid.error().where);
    }

    auto root = Codec::make_object();
    Codec::set(root, "specversion", Codec::make_string(cloud_event.specversion));
    Codec::set(root, "id", Codec::make_string(cloud_event.id));
    Codec::set(root, "source", Codec::make_string(cloud_event.source.view()));
    Codec::set(root, "type", Codec::make_string(cloud_event.type));

    if (cloud_event.datacontenttype) {
      Codec::set(root, "datacontenttype", Codec::make_string(*cloud_event.datacontenttype));
    }
    if (cloud_event.dataschema) {
      Codec::set(root, "dataschema", Codec::make_string(cloud_event.dataschema->view()));
    }
    if (cloud_event.subject) {
      Codec::set(root, "subject", Codec::make_string(*cloud_event.subject));
    }
    if (cloud_event.time) {
      Codec::set(root, "time", Codec::make_string(to_string(*cloud_event.time)));
    }

    for (const auto& [name, attribute] : cloud_event.extensions) {
      auto encoded = encode_attribute(attribute);
      if (!encoded) {
        return fail(encoded.error().code, encoded.error().detail, name);
      }
      Codec::set(root, name, std::move(*encoded));
    }

    if (auto stored = encode_data(root, cloud_event.data); !stored) {
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

  // --- decode --------------------------------------------------------------

  /// \brief Read one event from a JSON document.
  [[nodiscard]] static auto from_value(const value& document) -> result<event> {
    if (Codec::kind_of(document) != json::kind::object) {
      return fail(errc::parse_error, "a CloudEvent must be a JSON object");
    }

    event cloud_event{.id = {}, .source = {}, .type = {}};

    auto required = [&](std::string_view name, auto assign) -> result<void> {
      const auto* member = Codec::find(document, name);
      if (member == nullptr) {
        return fail(errc::missing_required_attribute, "required attribute is absent",
                    std::string{name});
      }
      auto text = Codec::as_string(*member);
      if (!text) {
        return fail(errc::invalid_attribute_value, "must be a JSON string", std::string{name});
      }
      assign(*text);
      return {};
    };

    if (auto read = required("specversion", [&](std::string_view v) { cloud_event.specversion = v; });
        !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (cloud_event.specversion != "1.0") {
      return fail(errc::unsupported_spec_version, "this SDK implements CloudEvents 1.0 only",
                  "specversion");
    }
    if (auto read = required("id", [&](std::string_view v) { cloud_event.id = v; }); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = required("source", [&](std::string_view v) { cloud_event.source = uri_ref{std::string{v}}; });
        !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = required("type", [&](std::string_view v) { cloud_event.type = v; }); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }

    auto optional_string = [&](std::string_view name) -> result<std::optional<std::string>> {
      const auto* member = Codec::find(document, name);
      if (member == nullptr || Codec::kind_of(*member) == json::kind::null) {
        return std::optional<std::string>{};
      }
      auto text = Codec::as_string(*member);
      if (!text) {
        return fail(errc::invalid_attribute_value, "must be a JSON string", std::string{name});
      }
      return std::optional<std::string>{std::string{*text}};
    };

    auto content = optional_string("datacontenttype");
    if (!content) {
      return fail(content.error().code, content.error().detail, content.error().where);
    }
    cloud_event.datacontenttype = *content;

    auto schema = optional_string("dataschema");
    if (!schema) {
      return fail(schema.error().code, schema.error().detail, schema.error().where);
    }
    if (*schema) {
      cloud_event.dataschema = uri{**schema};
    }

    auto subject_attribute = optional_string("subject");
    if (!subject_attribute) {
      return fail(subject_attribute.error().code, subject_attribute.error().detail,
                  subject_attribute.error().where);
    }
    cloud_event.subject = *subject_attribute;

    if (auto time_text = optional_string("time"); !time_text) {
      return fail(time_text.error().code, time_text.error().detail, time_text.error().where);
    } else if (*time_text) {
      auto parsed = parse_timestamp(**time_text);
      if (!parsed) {
        return fail(parsed.error().code, parsed.error().detail, "time");
      }
      cloud_event.time = *parsed;
    }

    if (auto stored = decode_data(document, cloud_event); !stored) {
      return fail(stored.error().code, stored.error().detail, stored.error().where);
    }

    // Anything not a context attribute is an extension. JSON has no room for the
    // CloudEvents attribute type, so the type is inferred and documented as lossy;
    // the typed extension structs recover it.
    result<void> extension_error{};
    Codec::for_each_member(document, [&](std::string_view name, const value& member) {
      if (!extension_error || reserved_name(name)) {
        return;
      }
      // JSON format section 2.2: an attribute encoded as null MUST be treated as
      // unset. The optional context attributes already do this; an extension is
      // no different, and the format specification's own example carries an
      // "unsetextension": null member to demonstrate it.
      if (Codec::kind_of(member) == json::kind::null) {
        return;
      }
      // An unknown member becomes an extension only if its name is one the spec
      // allows. Accepting others would let decode return an event that
      // validate() rejects, so the same document would decode and then fail to
      // re-encode.
      if (!valid_attribute_name(name)) {
        extension_error = fail(errc::invalid_attribute_name,
                               "extension names must match [a-z0-9]+", std::string{name});
        return;
      }
      auto decoded = decode_attribute(member);
      if (!decoded) {
        extension_error = fail(decoded.error().code, decoded.error().detail, std::string{name});
        return;
      }
      cloud_event.extensions.insert_or_assign(std::string{name}, std::move(*decoded));
    });
    if (!extension_error) {
      return fail(extension_error.error().code, extension_error.error().detail,
                  extension_error.error().where);
    }

    // The decoder must not hand back an event the encoder would refuse, so the
    // same document cannot decode and then fail to re-encode.
    if (auto valid = cloud_event.validate(); !valid) {
      return fail(valid.error().code, valid.error().detail, valid.error().where);
    }

    return cloud_event;
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

  [[nodiscard]] static auto decode_data(const value& document, event& cloud_event) -> result<void> {
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
      cloud_event.data = std::move(*decoded);
      return {};
    }

    if (data == nullptr) {
      return {};
    }

    // A JSON string under `data` is the payload itself when the content type says
    // it is not JSON. Otherwise the member is carried through as JSON.
    const bool declared_non_json =
        cloud_event.datacontenttype && !is_json_content_type(*cloud_event.datacontenttype);
    if (declared_non_json && Codec::kind_of(*data) == json::kind::string) {
      auto text = Codec::as_string(*data);
      if (!text) {
        return fail(errc::invalid_attribute_value, "data must be a JSON string", "data");
      }
      cloud_event.data = std::string{*text};
      return {};
    }

    cloud_event.data = json_text{.raw = Codec::dump(*data)};
    return {};
  }
};

}  // namespace ce::inline v1
