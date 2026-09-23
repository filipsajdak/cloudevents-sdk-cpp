#pragma once

/// \file
/// \brief The parity assertions, written once and run against both backends.
///
/// Deliberately free of `// spec:` markers: the markers live in
/// describe_parity_test.cpp, which is the file the requirements name.

#include <string>
#include <string_view>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/describe.hpp>

namespace ce_parity {

/// \brief What a described type is expected to look like, whichever backend
/// produced the description.
struct expectation {
  std::string_view label;
  std::vector<std::string_view> wire_names;
};

/// \brief Assert a type's description matches, and that visiting it agrees with
/// the names it reports.
template <ce::v2::described T>
void check(const expectation& expected) {
  using namespace boost::ut;

  expect(ce::v2::field_count<T> == expected.wire_names.size())
      << expected.label << ": field_count";

  const auto names = ce::v2::field_names<T>();
  for (std::size_t i = 0; i < expected.wire_names.size() && i < names.size(); ++i) {
    expect(names[i] == expected.wire_names[i])
        << expected.label << ": name " << i << " is " << names[i];
  }

  // Visiting must report the same names in the same order as field_names(),
  // which is what makes the two interfaces interchangeable for a caller.
  T object{};
  std::vector<std::string_view> visited;
  ce::v2::for_each_field(object, [&visited](std::string_view visited_name, auto&) {
    visited.push_back(visited_name);
  });
  expect(visited.size() == expected.wire_names.size()) << expected.label << ": visit count";
  for (std::size_t i = 0; i < visited.size() && i < expected.wire_names.size(); ++i) {
    expect(visited[i] == expected.wire_names[i]) << expected.label << ": visit order " << i;
  }

  // A const object must visit the same way.
  const T& const_object = object;
  std::vector<std::string_view> const_visited;
  ce::v2::for_each_field(const_object, [&const_visited](std::string_view visited_name, const auto&) {
    const_visited.push_back(visited_name);
  });
  expect(const_visited == visited) << expected.label << ": const visit differs";
}

}  // namespace ce_parity
