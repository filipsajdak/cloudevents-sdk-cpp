#include <boost/ut.hpp>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <cstdint>
#include <string>
#include <string_view>
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

[[nodiscard]] auto base_event() -> ce::event {
  return ce::event{
      .id = "1",
      .source = "/spec/test",
      .type = "com.example.thing",
  };
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
    ce::event subject = base_event();
    subject.dataschema = ce::uri{"https://example.test/schema"};
    subject.subject = "s";
    subject.time = *ce::parse_timestamp("2026-09-21T00:00:00Z");
    subject.datacontenttype = "text/plain";
    expect(bool{subject.set_extension("alpha", ce::attribute_value{std::string{"a"}})});
    expect(bool{subject.set_extension("beta", ce::attribute_value{std::int32_t{2}})});

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});

    const std::vector<std::string> expected = {
        "x_specversion", "x_id",    "x_source", "x_type", "x_dataschema",
        "x_subject",     "x_time",  "x_alpha",  "x_beta", "content-type",
    };
    expect(field_names(fields) == expected);
  };

  "the content type is the only unprefixed field"_test = [] {
    ce::event subject = base_event();
    subject.datacontenttype = "application/json";

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
    ce::raw_headers fields;
    fields.add("X_ID", "caller's own");

    ce::event subject = base_event();
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
    ce::raw_headers fields;
    fields.add("x_specversion", "1.0");
    fields.add("x_id", "1");
    fields.add("x_source", "/s");
    fields.add("x_type", "t");
    fields.add("x_ABC", "value");

    auto subject = binding::read_attributes<exact_traits>(fields);
    expect(!subject);
    if (!subject) {
      expect(subject.error().code == ce::errc::invalid_attribute_name);
      expect(subject.error().where == "ABC"sv);
    }
  };

  "a prefix in the wrong case is not a prefix"_test = [] {
    ce::raw_headers fields;
    fields.add("x_specversion", "1.0");
    fields.add("x_id", "1");
    fields.add("x_source", "/s");
    fields.add("x_type", "t");
    fields.add("X_extra", "ignored");

    auto subject = binding::read_attributes<exact_traits>(fields);
    expect(bool{subject});
    if (subject) {
      expect(subject->extensions.empty());
    }
  };
};

// spec: SYS-BIND-0001
// spec: SWR-BIND-0005
const boost::ut::suite<"binding-core-round-trip"> binding_core_round_trip = [] {
  using namespace boost::ut;

  "attributes survive a write and a read"_test = [] {
    ce::event subject = base_event();
    subject.subject = "s";
    subject.time = *ce::parse_timestamp("2026-09-21T00:00:00Z");
    expect(bool{subject.set_extension("alpha", ce::attribute_value{std::string{"a"}})});

    ce::raw_headers fields;
    expect(bool{binding::write_attributes<exact_traits>(subject, fields)});

    auto read_back = binding::read_attributes<exact_traits>(fields);
    expect(bool{read_back});
    if (read_back) {
      expect(read_back->id == subject.id);
      expect(bool{read_back->source == subject.source});
      expect(read_back->type == subject.type);
      expect(bool{read_back->subject == subject.subject});
      expect(bool{read_back->time == subject.time});
      expect(read_back->extensions.size() == 1U);
    }
  };

  "a value the traits refuse fails the write, naming the attribute"_test = [] {
    ce::event subject = base_event();
    subject.subject = std::string{"\xC3"};  // a truncated two-byte sequence

    ce::raw_headers fields;
    auto written = binding::write_attributes<exact_traits>(subject, fields);
    expect(!written);
    if (!written) {
      expect(written.error().code == ce::errc::invalid_utf8);
      expect(written.error().where == "subject"sv);
    }
  };

  "a missing required attribute names the first one absent"_test = [] {
    ce::raw_headers fields;
    fields.add("x_specversion", "1.0");
    fields.add("x_id", "1");
    fields.add("x_source", "/s");

    auto subject = binding::read_attributes<exact_traits>(fields);
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

    auto subject = binding::read_attributes<exact_traits>(fields);
    expect(!subject);
    if (!subject) {
      expect(subject.error().code == ce::errc::invalid_argument);
      expect(subject.error().where == "datacontenttype"sv);
    }
  };

  "the body round-trips as bytes or as JSON text"_test = [] {
    ce::event subject = base_event();
    subject.datacontenttype = "application/json";
    subject.data = ce::json_text{.raw = "{\"a\":1}"};

    ce::message out;
    binding::write_body(subject, out);
    expect(out.body.size() == 7U);

    ce::event read_back = base_event();
    read_back.datacontenttype = "application/json";
    binding::read_body(out.body, read_back);
    expect(bool{read_back.data == subject.data});
  };
};

int main() {}
