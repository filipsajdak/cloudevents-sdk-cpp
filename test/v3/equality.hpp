#pragma once

/// \file
/// \brief Equality where ut's operators are not in scope.
///
/// `using namespace boost::ut` brings ut's generic `operator==(T&&, T&&)` into
/// every suite. When both operands have the same const type - two optional
/// attributes read through const accessors - it is viable and ties with
/// std::optional's own operator, which GCC 16 at C++26 reports as ambiguous.
/// Compared here, only the standard operator is found.

namespace ce_test {

[[nodiscard]] constexpr auto equal(const auto& left, const auto& right) -> bool {
  return left == right;
}

}  // namespace ce_test
