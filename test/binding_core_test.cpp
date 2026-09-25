#include <boost/ut.hpp>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>
#include "equality.hpp"
#include "mini_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The HTTP binding exercises the core with case_sensitive_names == false and a
// percent-encoding value codec. The other half of every `if constexpr` in the
// core is unreachable from it, so these suites drive the core through a traits
// type that takes the opposite branch everywhere: byte-exact names, no escaping.
//
// That traits type is deliberately not Kafka's. A stand-in makes the core's
// contract the thing under test; the Kafka binding will bring its own suite for
// its own rules.

namespace {

using namespace std::string_view_literals;

namespace binding = ce::binding;

/// Byte-exact field names, values passed through unescaped but validated as
/// UTF-8, which is the shape every non-HTTP binding has.
struct exact_traits {
  static constexpr std::string_view attribute_prefix = "x_";
  static constexpr std::string_view content_type_header = "content-type";
  static constexpr bool case_sensitive_names = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> ce::result<std::string> {
    if (!ce::detail::is_valid_utf8(text)) {
      return ce::fail(ce::errc::invalid_utf8, "attribute value is not well-formed UTF-8");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> ce::result<std::string> {
    return encode_value(text);
  }
};

static_assert(binding::binding_traits<exact_traits>);

/// Negative cases for the concept. At namespace scope because a local class may
/// not have static data members, and [[maybe_unused]] because nothing ever
/// instantiates them: the concept check is the whole point, and clang reports an
/// unused internal-linkage constant as an error where gcc says nothing.
struct no_prefix {
  [[maybe_unused]] static constexpr std::string_view content_type_header = "content-type";
  [[maybe_unused]] static constexpr bool case_sensitive_names = true;
  static auto encode_value(std::string_view) -> ce::result<std::string> { return std::string{}; }
  static auto decode_value(std::string_view) -> ce::result<std::string> { return std::string{}; }
};

/// A value codec returning a bare string cannot report a refusal.
struct bare_string {
  [[maybe_unused]] static constexpr std::string_view attribute_prefix = "x_";
  [[maybe_unused]] static constexpr std::string_view content_type_header = "content-type";
  [[maybe_unused]] static constexpr bool case_sensitive_names = true;
  static auto encode_value(std::string_view) -> std::string { return {}; }
  static auto decode_value(std::string_view) -> std::string { return {}; }
};

static_assert(!binding::binding_traits<no_prefix>);
static_assert(!binding::binding_traits<bare_string>);

using namespace ce::literals;

[[nodiscard]] auto base_event(ce::event::options rest = {}) -> ce::event {
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// What a binding does with a message that carries no body: read the attributes,
/// then build. Absence of a required attribute is reported by the second step.
[[nodiscard]] auto read_event(const ce::raw_headers& fields) -> ce::result<ce::event> {
  auto under_construction = binding::read_attributes<exact_traits>(fields);
  if (!under_construction) {
    return ce::fail(under_construction.error().code, under_construction.error().detail,
                    under_construction.error().where);
  }
  return std::move(*under_construction).build();
}

[[nodiscard]] auto field_names(const ce::raw_headers& fields) -> std::vector<std::string> {
  std::vector<std::string> names;
  for (const auto& [name, value] : fields) {
    names.push_back(name);
  }
  return names;
}

}  // namespace

// spec: SWR-MSG-0002
const boost::ut::suite<"message-headers-from-field-list"> message_headers_field_list = [] {
  using namespace boost::ut;

  "a braced list builds the same fields as repeated add calls"_test = [] {
    const ce::raw_headers listed{
        {"x_specversion", "1.0"},
        {"x_id", "1"},
        {"x_source", "/s"},
    };

    ce::raw_headers added;
    added.add("x_specversion", "1.0");
    added.add("x_id", "1");
    added.add("x_source", "/s");

    expect(listed == added);
    expect(listed.size() == 3U);
  };

  // add semantics, not set: a message that arrived carrying a field twice is
  // exactly what a test written against this constructor needs to describe.
  "the order is kept and a repeated name is kept twice"_test = [] {
    const ce::raw_headers fields{
        {"x_id", "first"},
        {"x_other", "v"},
        {"x_id", "second"},
    };

    expect(fields.size() == 3U);
    expect(field_names(fields) == std::vector<std::string>{"x_id", "x_other", "x_id"});

    // find returns the first, which is the behaviour the duplicate exists to pin.
    const std::string* found = fields.find_exact("x_id");
    expect(found != nullptr);
    if (found != nullptr) {
      expect(*found == "first"sv);
    }
  };

  "an empty braced list is an empty set of fields"_test = [] {
    const ce::raw_headers none{};
    expect(none.empty());
    expect(none.size() == 0U);
  };
};

// spec: SWR-BIND-0001
const boost::ut::suite<"binding-core-traits"> binding_core_traits = [] {
  using namespace boost::ut;

  "a traits type missing a member is not binding_traits"_test = [] {
    expect(!binding::binding_traits<no_prefix>);
  };

  "a value codec returning a bare string is not binding_traits"_test = [] {
    expect(!binding::binding_traits<bare_string>);
  };

  "the stand-in traits satisfy the concept"_test = [] {
    expect(binding::binding_traits<exact_traits>);
  };
};

// spec: SWR-BIND-0002
const boost::ut::suite<"binding-core-emission-order"> binding_core_emission_order = [] {
  using namespace boost::ut;

  "attributes are emitted in the order the contract fixes"_test = [] {
    const auto when = ce::parse_timestamp("2026-09-21T00:00:00Z");
    expect(when.has_value());
    if (!when) {
      return;
    }
    const ce::event subject = base_event({
        .datacontenttype = "text/plain"_mediatype,
        .dataschema = "https://example.test/schema"_dataschema,
        .subject = "s"_subject,
        .time = *when,
        .extensions = {{"alpha"_ext, std::string{"a"}}, {"beta"_ext, std::int32_t{2}}},
    });

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});

    const std::vector<std::string> expected = {
        "x_specversion", "x_id",    "x_source", "x_type", "x_dataschema",
        "x_subject",     "x_time",  "x_alpha",  "x_beta", "content-type",
    };
    expect(field_names(fields) == expected);
  };

  // String, URI and URI-reference extensions reach the value codec as views of
  // the event's own text; every other type is rendered first. Both paths must
  // write what render_attribute says the value is.
  "every extension type is written as render_attribute renders it"_test = [] {
    const auto when = ce::parse_timestamp("2026-09-21T00:00:00Z");
    expect(when.has_value());
    if (!when) {
      return;
    }
    const ce::event subject = base_event({
        .extensions =
            {
                {"text"_ext, std::string{"plain text"}},
                {"empty"_ext, std::string{}},
                {"link"_ext, ce::uri{"https://example.test/x"}},
                {"relref"_ext, ce::uri_ref{"/relative"}},
                {"flag"_ext, true},
                {"count"_ext, std::int32_t{-7}},
                {"blob"_ext, ce::binary{std::byte{0x01}, std::byte{0xFF}}},
                {"seen"_ext, *when},
            },
    });

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});
    for (const auto& [name, attribute] : subject.extensions()) {
      const std::string* written = fields.find_exact(std::string{"x_"}.append(name.view()));
      expect(written != nullptr) << name.view();
      if (written != nullptr) {
        expect(*written == binding::render_attribute(attribute)) << name.view();
      }
    }
  };

  "an extension string the value codec refuses is still refused"_test = [] {
    const ce::event subject = base_event({.extensions = {{"bad"_ext, std::string{"\xC3"}}}});

    ce::raw_headers fields;
    const auto written = binding::write_attributes<exact_traits>(subject, fields);
    expect(!written.has_value());
    if (!written) {
      expect(written.error().code == ce::errc::invalid_utf8);
      expect(written.error().where == "bad"sv);
    }
  };

  "the content type is the only unprefixed field"_test = [] {
    const ce::event subject = base_event({.datacontenttype = "application/json"_mediatype});

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});
    expect(fields.find_exact("content-type") != nullptr);
    expect(fields.find_exact("x_datacontenttype") == nullptr);
  };
};

// spec: SWR-BIND-0003
const boost::ut::suite<"binding-core-name-case"> binding_core_name_case = [] {
  using namespace boost::ut;

  "a case-sensitive binding leaves a differently-cased field alone"_test = [] {
    ce::raw_headers fields{{"X_ID", "caller's own"}};

    const ce::event subject = base_event();
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});

    const std::string* kept = fields.find_exact("X_ID");
    expect(kept != nullptr);
    if (kept != nullptr) {
      expect(*kept == "caller's own"sv);
    }
    const std::string* written = fields.find_exact("x_id");
    expect(written != nullptr);
    if (written != nullptr) {
      expect(*written == "1"sv);
    }
  };

  "a case-sensitive binding does not lower an attribute name"_test = [] {
    const ce::raw_headers fields{
        {"x_specversion", "1.0"}, {"x_id", "1"}, {"x_source", "/s"}, {"x_type", "t"},
        {"x_ABC", "value"},
    };

    auto subject = read_event(fields);
    expect(!subject);
    if (!subject) {
      expect(subject.error().code == ce::errc::invalid_attribute_name);
      expect(subject.error().where == "ABC"sv);
    }
  };

  "a prefix in the wrong case is not a prefix"_test = [] {
    const ce::raw_headers fields{
        {"x_specversion", "1.0"}, {"x_id", "1"}, {"x_source", "/s"}, {"x_type", "t"},
        {"X_extra", "ignored"},
    };

    auto subject = read_event(fields);
    expect(bool{subject});
    if (subject) {
      expect(subject->extensions().empty());
    }
  };
};

// spec: SYS-BIND-0001
// spec: SWR-BIND-0005
// spec: SWR-BIND-0006
const boost::ut::suite<"binding-core-round-trip"> binding_core_round_trip = [] {
  using namespace boost::ut;

  "attributes survive a write and a read"_test = [] {
    const auto when = ce::parse_timestamp("2026-09-21T00:00:00Z");
    expect(when.has_value());
    if (!when) {
      return;
    }
    const ce::event subject = base_event({
        .subject = "s"_subject,
        .time = *when,
        .extensions = {{"alpha"_ext, std::string{"a"}}},
    });

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});

    auto read_back = read_event(fields);
    expect(bool{read_back});
    if (read_back) {
      expect(read_back->id() == subject.id());
      expect(bool{read_back->source() == subject.source()});
      expect(read_back->type() == subject.type());
      expect(ce_test::equal(read_back->subject(), subject.subject()));
      expect(ce_test::equal(read_back->time(), subject.time()));
      expect(read_back->extensions().size() == 1U);
    }
  };

  // A context attribute can no longer carry the bytes: `subject` refuses them
  // where it is made. An extension's value is not a context attribute and is
  // not validated, so it is the path by which the traits can still be handed
  // text they refuse, and the write must name the attribute that carried it.
  "a value the traits refuse fails the write, naming the attribute"_test = [] {
    const std::string truncated{"\xC3"};  // a truncated two-byte sequence
    expect(!ce::subject::make(truncated).has_value());

    const ce::event subject = base_event({.extensions = {{"alpha"_ext, truncated}}});

    ce::raw_headers fields;
    auto written = binding::write_attributes<exact_traits>(subject, fields);
    expect(!written);
    if (!written) {
      expect(written.error().code == ce::errc::invalid_utf8);
      expect(written.error().where == "alpha"sv);
    }
  };

  "a missing required attribute names the first one absent"_test = [] {
    const ce::raw_headers fields{{"x_specversion", "1.0"}, {"x_id", "1"}, {"x_source", "/s"}};

    auto subject = read_event(fields);
    expect(!subject);
    if (!subject) {
      expect(subject.error().code == ce::errc::missing_required_attribute);
      expect(subject.error().where == "type"sv);
    }
  };

  // A prefixed datacontenttype used to reach the extension branch, pass the name
  // check, be stored as an extension, and only then be refused by validate() as a
  // reserved name - a complaint about the name rather than about the field having
  // no place in this binding. The caller's fix is to move the media type to the
  // content-type field, and nothing in the old diagnosis said so.
  "a prefixed datacontenttype is refused where the binding has a content-type field"_test = [] {
    const ce::raw_headers fields{
        {"x_specversion", "1.0"},
        {"x_id", "1"},
        {"x_source", "/s"},
        {"x_type", "t"},
        {"x_datacontenttype", "application/json"},
    };

    auto subject = read_event(fields);
    expect(!subject);
    if (!subject) {
      expect(subject.error().code == ce::errc::invalid_argument);
      expect(subject.error().where == "datacontenttype"sv);
    }
  };

  // The media type is an argument, so the payload is decided by what the caller
  // passes rather than by what a half-built event happened to hold.
  "the body round-trips as bytes or as JSON text"_test = [] {
    const ce::event subject = base_event({.datacontenttype = "application/json"_mediatype,
                                          .data = ce::json_text{.raw = "{\"a\":1}"}});

    ce::message out;
    binding::write_body(subject, out);
    expect(out.body.size() == 7U);

    expect(bool{binding::read_body(out.body, subject.datacontenttype()) == subject.data()});
    expect(std::holds_alternative<ce::binary>(binding::read_body(out.body, std::nullopt)));
    expect(std::holds_alternative<std::monostate>(
        binding::read_body(ce::binary{}, subject.datacontenttype())));
  };

  "a document body is its compact serialisation, and reads back as JSON text"_test = [] {
    const auto parsed = ce::test::mini_codec::parse(R"({ "a" : [1, 2] })");
    expect(parsed.has_value());
    if (!parsed) {
      return;
    }
    const ce::event subject =
        base_event({.datacontenttype = "application/json"_mediatype,
                    .data = ce::json_document::make<ce::test::mini_codec>(*parsed)});

    ce::message out;
    binding::write_body(subject, out);
    const auto body_read = binding::read_body(out.body, subject.datacontenttype());
    expect(std::holds_alternative<ce::json_text>(body_read));
    if (const auto* text = std::get_if<ce::json_text>(&body_read)) {
      expect(text->raw == R"({"a":[1,2]})"sv);
    }
  };
};

int main() {}
