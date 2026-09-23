#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/attributes.hpp>

// spec: SYS-MSG-0001
namespace ce::v2 {

// spec: SWR-HTTP-0002
class raw_headers {
 public:
  using entry = std::pair<std::string, std::string>;

  raw_headers() = default;

  // spec: SWR-MSG-0002
  raw_headers(std::initializer_list<entry> fields) : entries_{fields} {}

  void add(std::string name, std::string value) {
    entries_.emplace_back(std::move(name), std::move(value));
  }

  void set(std::string name, std::string value) {
    const auto matches = [&name](const entry& candidate) {
      return detail::iequals(candidate.first, name);
    };
    std::erase_if(entries_, matches);
    entries_.emplace_back(std::move(name), std::move(value));
  }

  void set_exact(std::string name, std::string value) {
    const auto matches = [&name](const entry& candidate) { return candidate.first == name; };
    std::erase_if(entries_, matches);
    entries_.emplace_back(std::move(name), std::move(value));
  }

  [[nodiscard]] auto find(std::string_view name) const noexcept -> const std::string* {
    for (const auto& [candidate, value] : entries_) {
      if (detail::iequals(candidate, name)) {
        return &value;
      }
    }
    return nullptr;
  }

  // spec: SWR-MSG-0001
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

enum class name_matching : std::uint8_t {
  case_sensitive,
  case_insensitive,
};

// spec: SWR-MSG-0003
class headers {
 public:
  using entry = raw_headers::entry;

  headers() = default;

  // spec: SWR-MSG-0004
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

  [[nodiscard]] auto to_raw() const -> const raw_headers& { return fields_; }

  friend auto operator==(const headers&, const headers&) -> bool = default;

 private:
  raw_headers fields_;
};

// spec: SWR-HTTP-0001
struct message {
  raw_headers header_fields = {};
  binary body = {};

  friend auto operator==(const message&, const message&) -> bool = default;
};

enum class content_mode : std::uint8_t {
  binary_mode,
  structured,
  batched,
};

}  // namespace ce::v2

// spec: SWR-BUILD-0005
namespace ce::inline v3 {
using ce::v2::content_mode;
using ce::v2::headers;
using ce::v2::message;
using ce::v2::name_matching;
using ce::v2::raw_headers;
}  // namespace ce::inline v3
