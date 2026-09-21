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
class headers {
 public:
  using entry = std::pair<std::string, std::string>;

  headers() = default;

  /// \brief Build a set of fields from a literal list, in the order given.
  ///
  /// Carries the same meaning as repeated `add`: order is kept and a repeated
  /// name is kept twice, which is what a wire-shaped literal has to be able to
  /// say. `set` semantics would silently drop the duplicate a test was written
  /// to exercise.
  headers(std::initializer_list<entry> fields) : entries_{fields} {}

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

  friend auto operator==(const headers&, const headers&) -> bool = default;

 private:
  std::vector<entry> entries_;
};

/// \brief What a binding produces and consumes.
struct message {
  headers header_fields = {};
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
