#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// The attribute types carry their rule, so an invalid value has nowhere to live.
// Two things need checking and they are different in kind: that each rule refuses
// what the specification forbids, and that the storage underneath survives being
// copied and moved. The second is here because the storage is copy-on-write
// against static storage - a string_view that points either at a literal or at
// this object's own std::string - and getting that wrong is a use-after-move
// rather than a wrong answer. This suite is why the asan preset exists.

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;

/// True when `make` refused, with the code the rule names.
template <class T>
[[nodiscard]] auto refused(std::string_view text, ce::errc code) -> bool {
  const auto built = T::make(std::string{text});
  return !built.has_value() && built.error().code == code;
}

template <class T>
[[nodiscard]] auto accepts(std::string_view text) -> bool {
  const auto built = T::make(std::string{text});
  return built.has_value() && built->view() == text;
}

}  // namespace

// spec: SWR-CORE-0026
const boost::ut::suite<"attribute-types-refuse-invalid-text"> attribute_types_refuse = [] {
  using namespace boost::ut;

  // The rules the old validate() held, now held where the value is made.
  "a required attribute refuses empty text"_test = [] {
    expect(refused<ce::id>(""sv, ce::errc::missing_required_attribute));
    expect(refused<ce::source>(""sv, ce::errc::missing_required_attribute));
    expect(refused<ce::type>(""sv, ce::errc::missing_required_attribute));
  };

  // Present-but-empty is a violation rather than absence, so nullopt stays the
  // only way to say absent.
  "a present optional attribute refuses empty text"_test = [] {
    expect(refused<ce::subject>(""sv, ce::errc::invalid_attribute_value));
    expect(refused<ce::dataschema>(""sv, ce::errc::invalid_attribute_value));
    expect(refused<ce::datacontenttype>(""sv, ce::errc::invalid_attribute_value));
  };

  "a value that is not encodable is refused"_test = [] {
    expect(refused<ce::id>("\xC3"sv, ce::errc::invalid_utf8));
    expect(refused<ce::source>("\xE0\x80"sv, ce::errc::invalid_utf8));
  };

  "datacontenttype must be a media type"_test = [] {
    expect(refused<ce::datacontenttype>("not a media type"sv, ce::errc::invalid_content_type));
    expect(accepts<ce::datacontenttype>("application/json"sv));
    expect(accepts<ce::datacontenttype>("text/plain; charset=utf-8"sv));
  };

  "an extension name carries both of its rules"_test = [] {
    expect(refused<ce::extension_name>("Not-Lower"sv, ce::errc::invalid_attribute_name));
    expect(refused<ce::extension_name>(""sv, ce::errc::invalid_attribute_name));
    expect(refused<ce::extension_name>("id"sv, ce::errc::reserved_attribute_name));
    expect(refused<ce::extension_name>("data"sv, ce::errc::reserved_attribute_name));
    expect(accepts<ce::extension_name>("traceparent"sv));
  };

  // source is exempt from RFC 3986 on purpose: strict on produce, tolerant on
  // consume, and a producer using a looser URI-reference form still decodes.
  "source is checked for non-emptiness and encoding only"_test = [] {
    expect(accepts<ce::source>("/spec/test"sv));
    expect(accepts<ce::source>("not a uri at all"sv));
    expect(accepts<ce::source>("https://example.test/orders"sv));
  };

  // The failure carries the offending value, which the compile-time rule cannot
  // know and the runtime path can.
  "a refusal names the rule and the value"_test = [] {
    const auto built = ce::extension_name::make(std::string{"Not-Lower"});
    expect(!built.has_value());
    if (!built) {
      expect(built.error().detail == "extension names must match [a-z0-9]+");
      expect(built.error().where == "Not-Lower");
    }
  };

  // --- the storage, which is where a mistake is a crash rather than a wrong
  // answer. owned_ empty means the text is borrowed from a literal; any copy or
  // move has to re-point the view or leave it pointing into a moved-from string.
  // An empty owned_ is how the storage tells a borrowed value from an owned one,
  // so an owning value whose text were empty would be copied as if borrowed and
  // keep viewing the original's buffer. Every rule refusing the empty string is
  // what makes that state unreachable.
  "an owning instance is never empty"_test = [] {
    expect(!ce::id::make(std::string{}).has_value());
    expect(!ce::source::make(std::string{}).has_value());
    expect(!ce::type::make(std::string{}).has_value());
    expect(!ce::subject::make(std::string{}).has_value());
    expect(!ce::dataschema::make(std::string{}).has_value());
    expect(!ce::datacontenttype::make(std::string{}).has_value());
    expect(!ce::extension_name::make(std::string{}).has_value());
  };

  "a borrowed value survives being copied and moved"_test = [] {
    const ce::id borrowed = "A234-1234"_id;
    expect(borrowed.view() == "A234-1234"sv);

    const ce::id copied{borrowed};
    expect(copied.view() == "A234-1234"sv);

    ce::id to_move = "A234-1234"_id;
    const ce::id moved{std::move(to_move)};
    expect(moved.view() == "A234-1234"sv);
  };

  "an owned value survives being copied and moved"_test = [] {
    auto built = ce::id::make(std::string{"an identifier long enough to outgrow any small buffer"});
    expect(built.has_value());
    if (!built) {
      return;
    }

    const ce::id copied{*built};
    expect(copied.view() == "an identifier long enough to outgrow any small buffer"sv);

    const ce::id moved{std::move(*built)};
    expect(moved.view() == "an identifier long enough to outgrow any small buffer"sv);

    // The copy is independent of the moved-from original.
    expect(copied.view() == moved.view());
  };

  "assignment works in both directions between borrowed and owned"_test = [] {
    auto owned = ce::id::make(std::string{"an owned identifier long enough to allocate storage"});
    expect(owned.has_value());
    if (!owned) {
      return;
    }

    ce::id subject = "borrowed"_id;
    subject = *owned;
    expect(subject.view() == "an owned identifier long enough to allocate storage"sv);

    subject = ce::id{"borrowed-again"_id};
    expect(subject.view() == "borrowed-again"sv);

    ce::id other = "first"_id;
    std::swap(subject, other);
    expect(subject.view() == "first"sv);
    expect(other.view() == "borrowed-again"sv);
  };

  // A vector reallocating moves every element. If a move left the view pointing
  // at the old buffer this is where it shows.
  "values survive a container reallocating around them"_test = [] {
    std::vector<ce::id> many;
    for (int index = 0; index < 64; ++index) {
      auto built = ce::id::make("identifier-" + std::to_string(index) +
                                "-padded-out-so-the-string-allocates-rather-than-fitting");
      expect(built.has_value());
      if (built) {
        many.push_back(std::move(*built));
      }
    }
    expect(many.size() == 64U);
    expect(many.front().view().starts_with("identifier-0-"));
    expect(many.back().view().starts_with("identifier-63-"));
  };

  // The heterogeneous comparison is what keeps a map keyed by one of these
  // findable by string_view, without building a key to look up with.
  "a map keyed by an attribute type finds by string_view"_test = [] {
    std::map<ce::extension_name, int, std::less<>> extensions;
    auto name = ce::extension_name::make(std::string{"traceparent"});
    expect(name.has_value());
    if (!name) {
      return;
    }
    extensions.emplace(std::move(*name), 7);

    const auto found = extensions.find("traceparent"sv);
    expect(found != extensions.end());
    if (found != extensions.end()) {
      expect(found->second == 7);
    }
    expect(extensions.find("absent"sv) == extensions.end());
  };
};

// spec: SWR-CORE-0027
const boost::ut::suite<"attribute-literals-are-checked-when-compiled"> attribute_literals = [] {
  using namespace boost::ut;

  // The point of the literal path: a hardcoded attribute costs no run-time check
  // at all, because the rule ran while the translation unit was compiled.
  "a valid literal builds without a factory"_test = [] {
    const ce::id identifier = "A234-1234-1234"_id;
    const ce::source origin = "https://example.test/orders"_source;
    const ce::type kind = "com.example.order.placed"_type;

    expect(identifier.view() == "A234-1234-1234"sv);
    expect(origin.view() == "https://example.test/orders"sv);
    expect(kind.view() == "com.example.order.placed"sv);
  };

  "the direct-initialisation spelling works too"_test = [] {
    const ce::id identifier{"A234-1234-1234"};
    expect(identifier.view() == "A234-1234-1234"sv);
  };

  "every attribute has a literal"_test = [] {
    expect((ce::subject{"order-99"_subject}).view() == "order-99"sv);
    expect((ce::dataschema{"https://example.test/schema"_dataschema}).view() ==
           "https://example.test/schema"sv);
    expect((ce::datacontenttype{"application/json"_mediatype}).view() == "application/json"sv);
    expect((ce::extension_name{"traceparent"_ext}).view() == "traceparent"sv);
  };

  // An invalid literal is a compile error rather than a value, which no test in
  // this file can observe. attribute_literal_probe.cpp is the evidence: it is
  // registered as a test that must NOT compile.
  "the refusal is checked by a must-not-compile probe"_test = [] { expect(true); };

  "specversion has one inhabitant"_test = [] {
    static_assert(std::is_empty_v<ce::spec_version>);
    expect(ce::spec_version{}.view() == "1.0"sv);

    const auto decoded = ce::spec_version::make("0.3"sv);
    expect(!decoded.has_value());
    if (!decoded) {
      expect(decoded.error().code == ce::errc::unsupported_spec_version);
    }
    expect(ce::spec_version::make("1.0"sv).has_value());
  };
};

int main() {}
