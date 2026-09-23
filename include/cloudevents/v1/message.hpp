#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/v1/core.hpp>

namespace ce::v1 {

class headers {
 public:
  using entry = std::pair<std::string, std::string>;

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

  friend auto operator==(const headers&, const headers&) -> bool = default;

 private:
  std::vector<entry> entries_;
};

struct message {
  headers header_fields = {};
  binary body = {};

  friend auto operator==(const message&, const message&) -> bool = default;
};

enum class content_mode : std::uint8_t {
  binary_mode,
  structured,
  batched,
};

}  // namespace ce::v1
