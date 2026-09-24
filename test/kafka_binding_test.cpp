#include <boost/ut.hpp>

#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "codecs_under_test.hpp"
#include "equality.hpp"
#include "mini_codec.hpp"
#include "payload.hpp"

// Every assertion that touches a codec is a function template instantiated for
// each enabled codec, as in the HTTP suite. A suite that ran over one codec would
// be testing that codec rather than the binding.

namespace {

using namespace std::string_view_literals;

using namespace ce::literals;

[[nodiscard]] auto base_event(ce::event::options rest = {}) -> ce::event {
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// A record carrying the four attributes every event needs, plus the one field
/// a case is about.
[[nodiscard]] auto record_with(ce::raw_headers::entry extra) -> ce::message {
  return ce::message{.header_fields = {{"ce_specversion", "1.0"},
                                       {"ce_id", "1"},
                                       {"ce_source", "/s"},
                                       {"ce_type", "t"},
                                       std::move(extra)}};
}

template <class Codec>
void check_binary_mode(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({.subject = "s"_subject});

  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }

  // The underscore is the whole difference from HTTP, and the easiest thing to
  // carry over by accident.
  expect(laid_out->header_fields.find_exact("ce_specversion") != nullptr) << codec;
  expect(laid_out->header_fields.find_exact("ce_id") != nullptr) << codec;
  expect(laid_out->header_fields.find_exact("ce_source") != nullptr) << codec;
  expect(laid_out->header_fields.find_exact("ce_type") != nullptr) << codec;
  expect(laid_out->header_fields.find_exact("ce_subject") != nullptr) << codec;
  expect(laid_out->header_fields.find_exact("ce-id") == nullptr) << codec;

  auto read_back = ce::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
    expect(bool{read_back->source() == subject.source()}) << codec;
    expect(read_back->type() == subject.type()) << codec;
    expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
  }
}

template <class Codec>
void check_structured_mode(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event();

  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::structured);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const std::string* declared = laid_out->header_fields.find_exact("content-type");
  expect(declared != nullptr) << codec;
  if (declared != nullptr) {
    expect(*declared == "application/cloudevents+json"sv) << codec;
  }
  expect(laid_out->header_fields.find_exact("ce_id") == nullptr) << codec;

  auto read_back = ce::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
  }
}

constexpr auto document_payload = R"({"a":[1,2],"b":{"c":null}})"sv;

template <class Codec>
void check_document_round_trip(std::string_view codec) {
  using namespace boost::ut;

  const auto parsed = Codec::parse(document_payload);
  expect(parsed.has_value()) << codec;
  if (!parsed) {
    return;
  }
  const ce::event subject = base_event({
      .datacontenttype = "application/json"_mediatype,
      .data = ce::json_document::make<Codec>(Codec::copy(*parsed)),
  });

  for (const auto mode : {ce::content_mode::structured, ce::content_mode::binary_mode}) {
    auto laid_out = ce::kafka::to_message<Codec>(subject, mode);
    expect(bool{laid_out}) << codec;
    if (!laid_out) {
      continue;
    }
    auto read_back = ce::kafka::from_message<Codec>(*laid_out);
    expect(read_back && ce_test::same_json_payload<Codec>(read_back->data(), document_payload))
        << codec;
    if (mode == ce::content_mode::binary_mode) {
      expect(read_back && std::holds_alternative<ce::json_text>(read_back->data()))
          << codec << ": binary mode reads JSON text";
    }
  }
}

template <class Codec>
void check_content_type(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject =
      base_event({.datacontenttype = "text/plain"_mediatype, .data = std::string{"hello"}});

  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const std::string* declared = laid_out->header_fields.find_exact("content-type");
  expect(declared != nullptr) << codec;
  if (declared != nullptr) {
    expect(*declared == "text/plain"sv) << codec;
  }
  // The same attribute twice would leave a receiver with no rule for which wins.
  expect(laid_out->header_fields.find_exact("ce_datacontenttype") == nullptr) << codec;

  auto read_back = ce::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(ce_test::equal(read_back->datacontenttype(), subject.datacontenttype())) << codec;
  }
}

template <class Codec>
void check_values_are_unescaped_utf8(std::string_view codec) {
  using namespace boost::ut;

  // A space and U+00FC, both escaped by HTTP.
  const ce::event subject = base_event({.subject = "a b\xC3\xBC"_subject});

  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const std::string* written = laid_out->header_fields.find_exact("ce_subject");
  expect(written != nullptr) << codec;
  if (written != nullptr) {
    // Percent escapes here would reach every other SDK's consumer literally.
    expect(*written == "a b\xC3\xBC"sv) << codec;
  }
}

template <class Codec>
void check_invalid_utf8_is_refused(std::string_view codec) {
  using namespace boost::ut;

  // `subject` refuses the bytes where it is made; an extension's value is the
  // path by which the record header can still be handed them.
  const std::string truncated{"\xC3"};  // a truncated two-byte sequence
  expect(!ce::subject::make(truncated)) << codec;
  const ce::event subject = base_event({.extensions = {{"alpha"_ext, truncated}}});

  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(!laid_out) << codec;
  if (!laid_out) {
    expect(laid_out.error().code == ce::errc::invalid_utf8) << codec;
    expect(laid_out.error().where == "alpha"sv) << codec;
  }
}

template <class Codec>
void check_keys_are_byte_exact(std::string_view codec) {
  using namespace boost::ut;

  // Writing must not erase a differently-cased header the caller set.
  ce::message carrier{.header_fields = {{"CE_ID", "the caller's own"}}};
  const ce::event subject = base_event();
  auto written = ce::binding::write_attributes<ce::kafka::detail::kafka_traits>(
      subject, carrier.header_fields);
  expect(bool{written}) << codec;
  expect(carrier.header_fields.find_exact("CE_ID") != nullptr) << codec;

  // Reading must not accept a differently-cased prefix as an attribute.
  auto read_back = ce::kafka::from_message<Codec>(record_with({"CE_SOURCE", "/not-the-source"}));
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->source().view() == "/s"sv) << codec;
  }
}

template <class Codec>
void check_batch_refused(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event();
  auto laid_out = ce::kafka::to_message<Codec>(subject, ce::content_mode::batched);
  expect(!laid_out) << codec;
  if (!laid_out) {
    expect(laid_out.error().code == ce::errc::invalid_argument) << codec;
  }

  // The batch content type also starts with the structured one, so it must be
  // recognised first or this arrives at the format layer as a malformed object.
  const ce::message incoming{
      .header_fields = {{"content-type", "application/cloudevents-batch+json"}},
      .body = ce::to_bytes("[]"),
  };
  auto read_back = ce::kafka::from_message<Codec>(incoming);
  expect(!read_back) << codec;
  if (!read_back) {
    expect(read_back.error().code == ce::errc::invalid_argument) << codec;
  }
}

template <class Codec>
void check_not_a_cloudevent(std::string_view codec) {
  using namespace boost::ut;

  const ce::message incoming{
      .header_fields = {{"content-type", "application/json"}},
      .body = ce::to_bytes("{\"unrelated\":true}"),
  };

  auto read_back = ce::kafka::from_message<Codec>(incoming);
  expect(!read_back) << codec;
  if (!read_back) {
    expect(read_back.error().code == ce::errc::not_a_cloudevent) << codec;
  }
}

template <class Codec>
void check_record_and_key_mapper(std::string_view codec) {
  using namespace boost::ut;

  ce::event subject = base_event();
  expect(bool{subject.set(ce::ext::partitioning{.partitionkey = "customer-42"})}) << codec;
  const ce::event before = subject;

  // Not naming a mapper is the specification's default behaviour.
  auto plain = ce::kafka::to_record<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{plain}) << codec;
  if (plain) {
    expect(!plain->key.has_value()) << codec;
  }

  auto keyed = ce::kafka::to_record<Codec, ce::kafka::partitionkey_mapper>(
      subject, ce::content_mode::binary_mode);
  expect(bool{keyed}) << codec;
  if (keyed) {
    expect(keyed->key.has_value()) << codec;
    if (keyed->key) {
      expect(*keyed->key == "customer-42"sv) << codec;
    }
    // The attribute MUST still travel: a consumer reading the event rather than
    // the record would otherwise never see it.
    const std::string* still_there = keyed->value.header_fields.find_exact("ce_partitionkey");
    expect(still_there != nullptr) << codec;
    if (still_there != nullptr) {
      expect(*still_there == "customer-42"sv) << codec;
    }
  }

  // Mapping reads the event; it never edits it.
  expect(bool{subject == before}) << codec;

  // An event with no partitionkey gets no key, rather than an empty one.
  auto without = ce::kafka::to_record<Codec, ce::kafka::partitionkey_mapper>(
      base_event(), ce::content_mode::binary_mode);
  expect(bool{without}) << codec;
  if (without) {
    expect(!without->key.has_value()) << codec;
  }
}

}  // namespace

// spec: SWR-KAFKA-0001
const boost::ut::suite<"kafka-binary-mode"> kafka_binary_mode = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_binary_mode<C>(codec);
      check_structured_mode<C>(codec);
      check_document_round_trip<C>(codec);
    };
  });
};

// spec: SWR-KAFKA-0002
const boost::ut::suite<"kafka-content-type"> kafka_content_type = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_content_type<C>(codec); };
  });
};

// spec: SWR-KAFKA-0003
const boost::ut::suite<"kafka-header-values-are-utf8"> kafka_header_values_are_utf8 = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_values_are_unescaped_utf8<C>(codec);
      check_invalid_utf8_is_refused<C>(codec);
    };
  });
};

// spec: SWR-KAFKA-0004
const boost::ut::suite<"kafka-keys-are-byte-exact"> kafka_keys_are_byte_exact = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_keys_are_byte_exact<C>(codec); };
  });
};

// spec: SWR-KAFKA-0005
const boost::ut::suite<"kafka-batch-refused"> kafka_batch_refused = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_batch_refused<C>(codec); };
  });
};

// spec: SWR-KAFKA-0006
const boost::ut::suite<"kafka-not-a-cloudevent"> kafka_not_a_cloudevent = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_not_a_cloudevent<C>(codec); };
  });
};

// spec: SWR-KAFKA-0007
// spec: SWR-KAFKA-0008
// spec: SWR-KAFKA-0009
const boost::ut::suite<"kafka-record-and-key-mapper"> kafka_record_and_key_mapper = [] {
  using namespace boost::ut;

  static_assert(ce::kafka::key_mapper<ce::kafka::no_key_mapper>);
  static_assert(ce::kafka::key_mapper<ce::kafka::partitionkey_mapper>);
  static_assert(std::is_aggregate_v<ce::kafka::record>);

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_record_and_key_mapper<C>(codec); };
  });
};

int main() {}
