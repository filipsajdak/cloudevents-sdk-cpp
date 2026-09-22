#pragma once

/// \file
/// \brief A transport-neutral message. No HTTP library type appears in the SDK.

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/core.hpp>

namespace ce::inline v1 {

/// \brief Headers, in order, with case-insensitive lookup.
///
/// Order and duplicates are preserved because the HTTP binding permits repeated
/// headers, so a multimap that merged them would lose information a receiver may
/// need.
class raw_headers {
 public:
  using entry = std::pair<std::string, std::string>;

  raw_headers() = default;

  /// \brief Build a set of fields from a literal list, in the order given.
  ///
  /// Carries the same meaning as repeated `add`: order is kept and a repeated
  /// name is kept twice, which is what a wire-shaped literal has to be able to
  /// say. `set` semantics would silently drop the duplicate a test was written
  /// to exercise.
  raw_headers(std::initializer_list<entry> fields) : entries_{fields} {}

  void add(std::string name, std::string value) {
    entries_.emplace_back(std::move(name), std::move(value));
  }

  /// \brief Replace every header of this name, or add one when absent.
  void set(std::string name, std::string value) {
    const auto matches = [&name](const entry& candidate) {
      return detail::iequals(candidate.first, name);
    };
    std::erase_if(entries_, matches);
    entries_.emplace_back(std::move(name), std::move(value));
  }

  /// \brief Replace every header of this name, matching byte for byte.
  ///
  /// The case-insensitive `set` is right for HTTP, whose field names are
  /// case-insensitive, and wrong everywhere else: Kafka record headers, AMQP
  /// application-properties and MQTT user properties are all case-sensitive, so
  /// a case-insensitive erase would remove a header a caller had deliberately
  /// distinguished.
  void set_exact(std::string name, std::string value) {
    const auto matches = [&name](const entry& candidate) { return candidate.first == name; };
    std::erase_if(entries_, matches);
    entries_.emplace_back(std::move(name), std::move(value));
  }

  /// \brief The first header of this name, or nullptr.
  [[nodiscard]] auto find(std::string_view name) const noexcept -> const std::string* {
    for (const auto& [candidate, value] : entries_) {
      if (detail::iequals(candidate, name)) {
        return &value;
      }
    }
    return nullptr;
  }

  /// \brief The first header of exactly this name, or nullptr.
  [[nodiscard]] auto find_exact(std::string_view name) const noexcept -> const std::string* {
    for (const auto& [candidate, value] : entries_) {
      if (candidate == name) {
        return &value;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto contains(std::string_view name) const noexcept -> bool {
    return find(name) != nullptr;
  }

  /// \brief Whether a header of exactly this name is present.
  [[nodiscard]] auto contains_exact(std::string_view name) const noexcept -> bool {
    return find_exact(name) != nullptr;
  }

  [[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
  [[nodiscard]] auto end() const noexcept { return entries_.end(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return entries_.size(); }
  [[nodiscard]] auto empty() const noexcept -> bool { return entries_.empty(); }

  friend auto operator==(const raw_headers&, const raw_headers&) -> bool = default;

 private:
  std::vector<entry> entries_;
};

/// \brief How a binding decides whether two field names are one name.
///
/// HTTP field names are case-insensitive; Kafka record headers, AMQP
/// application-properties and MQTT user properties are not. Which rule applies
/// decides whether `ce-id` and `CE-ID` are the same attribute arriving twice or
/// two different fields, so it is an argument rather than an assumption.
enum class name_matching : std::uint8_t {
  case_sensitive,
  case_insensitive,
};

/// \brief Fields that hold at most one of any name (SWR-MSG-0003).
///
/// What a transport delivers is `raw_headers`, which carries whatever arrived.
/// This is what the SDK is willing to work from: a set in which no name appears
/// twice, so no code downstream has to decide which of two `ce-id` fields it
/// meant. There is no `add`, because a second field of the same name is the
/// state this type exists to exclude.
class headers {
 public:
  using entry = raw_headers::entry;

  headers() = default;

  /// \brief Adopt what a transport delivered, or say why it cannot be adopted.
  ///
  /// A repeated field name is a repeated CloudEvents attribute - the binding
  /// prefixes every attribute, so two fields carrying one attribute are two
  /// fields of one name. HTTP binding section 3.1.3 makes that malformed, and
  /// picking either one silently is a guess about which peer was right
  /// (SWR-MSG-0004).
  [[nodiscard]] static auto adopt(const raw_headers& delivered, name_matching matching)
      -> result<headers> {
    headers adopted;
    for (const auto& [name, value] : delivered) {
      const bool already = matching == name_matching::case_sensitive
                               ? adopted.fields_.contains_exact(name)
                               : adopted.fields_.contains(name);
      if (already) {
        return fail(errc::invalid_argument,
                    "two fields carry the same attribute, so which one is meant is undecidable",
                    name);
      }
      adopted.fields_.add(name, value);
    }
    return adopted;
  }

  void set(std::string name, std::string value) {
    fields_.set_exact(std::move(name), std::move(value));
  }

  [[nodiscard]] auto find(std::string_view name) const noexcept -> const std::string* {
    return fields_.find(name);
  }
  [[nodiscard]] auto find_exact(std::string_view name) const noexcept -> const std::string* {
    return fields_.find_exact(name);
  }
  [[nodiscard]] auto contains(std::string_view name) const noexcept -> bool {
    return fields_.contains(name);
  }
  [[nodiscard]] auto contains_exact(std::string_view name) const noexcept -> bool {
    return fields_.contains_exact(name);
  }

  [[nodiscard]] auto begin() const noexcept { return fields_.begin(); }
  [[nodiscard]] auto end() const noexcept { return fields_.end(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return fields_.size(); }
  [[nodiscard]] auto empty() const noexcept -> bool { return fields_.empty(); }

  /// \brief The fields, back in the permissive form a transport takes.
  [[nodiscard]] auto to_raw() const -> const raw_headers& { return fields_; }

  friend auto operator==(const headers&, const headers&) -> bool = default;

 private:
  raw_headers fields_;
};

/// \brief What a binding produces and consumes.
struct message {
  raw_headers header_fields = {};
  binary body = {};

  friend auto operator==(const message&, const message&) -> bool = default;
};

/// \brief How an event is laid out in a message.
enum class content_mode : std::uint8_t {
  /// Attributes in headers, the payload as the body.
  binary_mode,
  /// The whole event as a JSON document in the body.
  structured,
  /// An array of events as a JSON document in the body.
  batched,
};

}  // namespace ce::inline v1
