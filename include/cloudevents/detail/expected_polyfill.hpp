#pragma once

/// \file
/// \brief A deliberately small stand-in for std::expected at the C++20 floor.
///
/// This implements exactly the subset of std::expected the SDK is permitted to
/// use, and nothing else. That is the enforcement mechanism described in ADR-0002:
/// building the whole library and suite against this type makes any use of a
/// banned member (value(), value_or, error_or, and_then, or_else, transform,
/// transform_error, emplace, swap, operator==) a hard compile error. Deleting this
/// header once the floor reaches C++23 is then provably a no-op, because every
/// call site compiled against the smaller surface.
///
/// Two members are absent on purpose rather than by omission. value() throws
/// std::bad_expected_access, which SPEC section 9 decision D4 forbids; the monadic
/// operations would compile here only by growing the surface this type exists to
/// constrain.

#include <memory>
#include <type_traits>
#include <utility>

namespace ce::inline v1::detail::poly {

/// \brief The error carrier, mirroring std::unexpected's role.
template <class E>
class unexpected {
 public:
  constexpr explicit unexpected(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
      : error_{std::move(error)} {}

  [[nodiscard]] constexpr auto error() const& noexcept -> const E& { return error_; }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& { return std::move(error_); }

 private:
  E error_;
};

template <class E>
unexpected(E) -> unexpected<E>;

/// \brief Holds either a value or an error, with a narrow-contract accessor set.
///
/// operator*, operator-> and error() have preconditions rather than checks: they
/// never throw, which is what keeps the type usable under -fno-exceptions. Calling
/// them on the wrong alternative is undefined, exactly as it is for std::expected.
template <class T, class E>
class expected {
 public:
  using value_type = T;
  using error_type = E;

  constexpr expected()
    requires std::is_default_constructible_v<T>
      : has_value_{true}, value_{} {}

  // Implicit by design: std::expected converts from its value type, and every
  // `return some_value;` in a function returning result<T> relies on it.
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(T value) : has_value_{true}, value_{std::move(value)} {}

  // Implicit by design: this is what lets ce::fail() convert into any result<T>.
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(unexpected<E> error) : has_value_{false}, error_{std::move(error).error()} {}

  constexpr expected(const expected& other) : has_value_{other.has_value_} {
    if (has_value_) {
      std::construct_at(std::addressof(value_), other.value_);
    } else {
      std::construct_at(std::addressof(error_), other.error_);
    }
  }

  constexpr expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                std::is_nothrow_move_constructible_v<E>)
      : has_value_{other.has_value_} {
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

  [[nodiscard]] constexpr auto operator*() & noexcept -> T& { return value_; }
  [[nodiscard]] constexpr auto operator*() const& noexcept -> const T& { return value_; }
  [[nodiscard]] constexpr auto operator*() && noexcept -> T&& { return std::move(value_); }

  [[nodiscard]] constexpr auto operator->() noexcept -> T* { return std::addressof(value_); }
  [[nodiscard]] constexpr auto operator->() const noexcept -> const T* {
    return std::addressof(value_);
  }

  [[nodiscard]] constexpr auto error() & noexcept -> E& { return error_; }
  [[nodiscard]] constexpr auto error() const& noexcept -> const E& { return error_; }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& { return std::move(error_); }

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

/// \brief The void specialization: success carries nothing, so there is no
/// operator* to provide.
template <class E>
class expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  constexpr expected() noexcept : has_value_{true} {}

  // Implicit by design: this is what lets ce::fail() convert into any result<T>.
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  constexpr expected(unexpected<E> error) : has_value_{false}, error_{std::move(error).error()} {}

  [[nodiscard]] constexpr auto has_value() const noexcept -> bool { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] constexpr auto error() & noexcept -> E& { return error_; }
  [[nodiscard]] constexpr auto error() const& noexcept -> const E& { return error_; }
  [[nodiscard]] constexpr auto error() && noexcept -> E&& { return std::move(error_); }

 private:
  bool has_value_;
  E error_{};
};

}  // namespace ce::inline v1::detail::poly
