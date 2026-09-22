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
    auto root = Codec::make_object();
    Codec::set(root, "specversion", Codec::make_string(spec_version_of(cloud_event).view()));
    Codec::set(root, "id", Codec::make_string(id_of(cloud_event).view()));
    Codec::set(root, "source", Codec::make_string(source_of(cloud_event).view()));
    Codec::set(root, "type", Codec::make_string(type_of(cloud_event).view()));

    // Bound once rather than called twice: nothing says the second call yields
    // the engaged optional the first one tested.
    if (const auto& media_type = datacontenttype_of(cloud_event); media_type) {
      Codec::set(root, "datacontenttype", Codec::make_string(media_type->view()));
    }
    if (const auto& schema = dataschema_of(cloud_event); schema) {
      Codec::set(root, "dataschema", Codec::make_string(schema->view()));
    }
    if (const auto& named = subject_of(cloud_event); named) {
      Codec::set(root, "subject", Codec::make_string(named->view()));
    }
    if (const auto& when = time_of(cloud_event); when) {
      Codec::set(root, "time", Codec::make_string(to_string(*when)));
    }

    for (const auto& [name, attribute] : extensions_of(cloud_event)) {
      auto encoded = encode_attribute(attribute);
      if (!encoded) {
        return fail(encoded.error().code, encoded.error().detail, name.str());
      }
      Codec::set(root, name.view(), std::move(*encoded));
    }

    if (auto stored = encode_data(root, data_of(cloud_event)); !stored) {
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

    event::builder under_construction{};

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
      return assign(*text);
    };

    // Each required attribute goes through its own factory, which is the only
    // thing that can refuse the text the document carried.
    const auto store = []<class Attribute>(std::optional<Attribute>& slot) {
      return [&slot](std::string_view text) -> result<void> {
        auto made = Attribute::make(text);
        if (!made) {
          return fail(made.error().code, made.error().detail, made.error().where);
        }
        slot = std::move(*made);
        return {};
      };
    };

    if (auto read = required("specversion",
                             [](std::string_view text) -> result<void> {
                               auto version = spec_version::make(text);
                               if (!version) {
                                 return fail(version.error().code, version.error().detail,
                                             version.error().where);
                               }
                               return {};
                             });
        !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = required("id", store(under_construction.id)); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = required("source", store(under_construction.source)); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = required("type", store(under_construction.type)); !read) {
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

    // An absent optional attribute stays absent; a present one goes through its
    // factory, so the only way into the event is past its rule.
    const auto store_optional = [&]<class Attribute>(std::string_view name,
                                                     std::optional<Attribute>& slot) -> result<void> {
      auto text = optional_string(name);
      if (!text) {
        return fail(text.error().code, text.error().detail, text.error().where);
      }
      if (!*text) {
        return {};
      }
      auto made = Attribute::make(std::move(**text));
      if (!made) {
        return fail(made.error().code, made.error().detail, made.error().where);
      }
      slot = std::move(*made);
      return {};
    };

    if (auto read = store_optional("datacontenttype", under_construction.rest.datacontenttype);
        !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = store_optional("dataschema", under_construction.rest.dataschema); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }
    if (auto read = store_optional("subject", under_construction.rest.subject); !read) {
      return fail(read.error().code, read.error().detail, read.error().where);
    }

    if (auto time_text = optional_string("time"); !time_text) {
      return fail(time_text.error().code, time_text.error().detail, time_text.error().where);
    } else if (*time_text) {
      auto parsed = parse_timestamp(**time_text);
      if (!parsed) {
        return fail(parsed.error().code, parsed.error().detail, "time");
      }
      under_construction.rest.time = *parsed;
    }

    if (auto stored = decode_data(document, under_construction.rest); !stored) {
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
      // allows. Accepting others would let decode return an event the encoder
      // then refuses, so the same document would decode and fail to re-encode.
      auto attribute = extension_name::make(name);
      if (!attribute) {
        extension_error =
            fail(attribute.error().code, attribute.error().detail, std::string{name});
        return;
      }
      auto decoded = decode_attribute(member);
      if (!decoded) {
        extension_error = fail(decoded.error().code, decoded.error().detail, std::string{name});
        return;
      }
      under_construction.rest.extensions.insert_or_assign(std::move(*attribute),
                                                          std::move(*decoded));
    });
    if (!extension_error) {
      return fail(extension_error.error().code, extension_error.error().detail,
                  extension_error.error().where);
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
