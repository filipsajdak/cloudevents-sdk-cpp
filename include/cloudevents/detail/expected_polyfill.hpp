#pragma once

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace ce::v1::detail::poly {

// spec: SWR-ADOPT-0003
[[noreturn]] inline void read_of_the_alternative_an_expected_does_not_hold() { std::abort(); }

constexpr void require(bool holds) noexcept {
  if (!holds) {
    read_of_the_alternative_an_expected_does_not_hold();
  }
}

template <class E>
class unexpected {
 public:
  constexpr explicit unexpected(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
      : error_(std::move(error)) {}

  [[nodiscard]] constexpr auto error() const& noexcept -> const E& { return error_; }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& { return std::move(error_); }

 private:
  E error_;
};

template <class E>
unexpected(E) -> unexpected<E>;

// spec: SWR-CORE-0003
// spec: SWR-BUILD-0004
template <class T, class E>
class expected {
 public:
  using value_type = T;
  using error_type = E;

  constexpr expected()
    requires std::is_default_constructible_v<T>
      : has_value_(true), value_() {}

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(T value) : has_value_(true), value_(std::move(value)) {}

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(unexpected<E> error) : has_value_(false), error_(std::move(error).error()) {}

  constexpr expected(const expected& other) : has_value_(other.has_value_) {
    if (has_value_) {
      std::construct_at(std::addressof(value_), other.value_);
    } else {
      std::construct_at(std::addressof(error_), other.error_);
    }
  }

  constexpr expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                std::is_nothrow_move_constructible_v<E>)
      : has_value_(other.has_value_) {
    if (has_value_) {
      std::construct_at(std::addressof(value_), std::move(other.value_));
    } else {
      std::construct_at(std::addressof(error_), std::move(other.error_));
    }
  }

  constexpr auto operator=(const expected& other) -> expected& {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (has_value_) {
        std::construct_at(std::addressof(value_), other.value_);
      } else {
        std::construct_at(std::addressof(error_), other.error_);
      }
    }
    return *this;
  }

  constexpr auto operator=(expected&& other) noexcept(
      std::is_nothrow_move_constructible_v<T> &&
      std::is_nothrow_move_constructible_v<E>) -> expected& {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (has_value_) {
        std::construct_at(std::addressof(value_), std::move(other.value_));
      } else {
        std::construct_at(std::addressof(error_), std::move(other.error_));
      }
    }
    return *this;
  }

  constexpr ~expected() { destroy(); }

  [[nodiscard]] constexpr auto has_value() const noexcept -> bool { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] constexpr auto operator*() & noexcept -> T& {
    require(has_value_);
    return value_;
  }
  [[nodiscard]] constexpr auto operator*() const& noexcept -> const T& {
    require(has_value_);
    return value_;
  }
  [[nodiscard]] constexpr auto operator*() && noexcept -> T&& {
    require(has_value_);
    return std::move(value_);
  }

  [[nodiscard]] constexpr auto operator->() noexcept -> T* {
    require(has_value_);
    return std::addressof(value_);
  }
  [[nodiscard]] constexpr auto operator->() const noexcept -> const T* {
    require(has_value_);
    return std::addressof(value_);
  }

  [[nodiscard]] constexpr auto error() & noexcept -> E& {
    require(!has_value_);
    return error_;
  }
  [[nodiscard]] constexpr auto error() const& noexcept -> const E& {
    require(!has_value_);
    return error_;
  }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& {
    require(!has_value_);
    return std::move(error_);
  }

 private:
  constexpr void destroy() noexcept {
    if (has_value_) {
      std::destroy_at(std::addressof(value_));
    } else {
      std::destroy_at(std::addressof(error_));
    }
  }

  bool has_value_;
  union {
    T value_;
    E error_;
  };
};

template <class E>
class expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  constexpr expected() noexcept : has_value_(true), empty_() {}

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(unexpected<E> error) : has_value_(false), error_(std::move(error).error()) {}

  constexpr expected(const expected& other) : has_value_(other.has_value_), empty_() {
    if (!has_value_) {
      std::construct_at(std::addressof(error_), other.error_);
    }
  }

  constexpr expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<E>)
      : has_value_(other.has_value_), empty_() {
    if (!has_value_) {
      std::construct_at(std::addressof(error_), std::move(other.error_));
    }
  }

  constexpr auto operator=(const expected& other) -> expected& {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (!has_value_) {
        std::construct_at(std::addressof(error_), other.error_);
      }
    }
    return *this;
  }

  constexpr auto operator=(expected&& other) noexcept(std::is_nothrow_move_constructible_v<E>)
      -> expected& {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (!has_value_) {
        std::construct_at(std::addressof(error_), std::move(other.error_));
      }
    }
    return *this;
  }

  constexpr ~expected() { destroy(); }

  [[nodiscard]] constexpr auto has_value() const noexcept -> bool { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] constexpr auto error() & noexcept -> E& {
    require(!has_value_);
    return error_;
  }
  [[nodiscard]] constexpr auto error() const& noexcept -> const E& {
    require(!has_value_);
    return error_;
  }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& {
    require(!has_value_);
    return std::move(error_);
  }

 private:
  constexpr void destroy() noexcept {
    if (!has_value_) {
      std::destroy_at(std::addressof(error_));
    }
  }

  bool has_value_;
  struct empty {};
  union {
    empty empty_;
    E error_;
  };
};

}  // namespace ce::v1::detail::poly
