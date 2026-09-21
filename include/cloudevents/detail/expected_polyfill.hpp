#pragma once

/// \file
/// \brief A stand-in for std::expected implementing exactly the subset ADR-0002
/// permits, so building against it is what enforces that subset.
///
/// `value()` and the monadic operations are absent on purpose: `value()` throws,
/// which -fno-exceptions forbids.

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace ce::inline v1::detail::poly {

/// \brief Reading the alternative an expected does not hold.
///
/// Not constexpr, deliberately: a constant-evaluated wrong read becomes a
/// compile error naming this function. At run time it aborts rather than
/// returning a value the caller would go on to believe. std::expected leaves the
/// same read undefined, so the polyfill is the stricter of the two, and the
/// C++20 floor preset is where a mistake surfaces.
[[noreturn]] inline void read_of_the_alternative_an_expected_does_not_hold() { std::abort(); }

constexpr void require(bool holds) noexcept {
  if (!holds) {
    read_of_the_alternative_an_expected_does_not_hold();
  }
}

/// \brief The error carrier, mirroring std::unexpected's role.
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

/// \brief Holds either a value or an error.
///
/// `operator*`, `operator->` and `error()` have preconditions rather than checks
/// and never throw. Calling one on the wrong alternative is undefined, as for
/// std::expected.
template <class T, class E>
class expected {
 public:
  using value_type = T;
  using error_type = E;

  constexpr expected()
    requires std::is_default_constructible_v<T>
      : has_value_(true), value_() {}

  // Implicit by design, as std::expected is.
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(T value) : has_value_(true), value_(std::move(value)) {}

  // Implicit by design: ce::fail() converts into any result<T>.
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

/// \brief Success carries nothing, so there is no operator*.
///
/// The error lives in a union, as it does in the primary template above. An
/// earlier version held a default-constructed `E` unconditionally, which made
/// `error()` on a successful result return `errc{0}` - a code with no
/// enumerator, printing as "unknown" - where std::expected leaves that read
/// undefined. A misread therefore behaved differently depending on which backend
/// the build selected, which is the one thing the polyfill must never do.
template <class E>
class expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  constexpr expected() noexcept : has_value_(true), empty_() {}

  // Implicit by design: ce::fail() converts into any result<T>.
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

}  // namespace ce::inline v1::detail::poly
