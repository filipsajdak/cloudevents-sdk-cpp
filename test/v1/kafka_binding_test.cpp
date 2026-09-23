#include <boost/ut.hpp>

#include <cloudevents/v1/binding/kafka.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/v1/extensions.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/message.hpp>
#include <cloudevents/result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "codecs_under_test.hpp"
#include "mini_codec.hpp"

// Every assertion that touches a codec is a function template instantiated for
// each enabled codec, as in the HTTP suite. A suite that ran over one codec would
// be testing that codec rather than the binding.

namespace {

using namespace std::string_view_literals;

[[nodiscard]] auto base_event() -> ce::v1::event {
  return ce::v1::event{
      .id = "1",
      .source = "/spec/test",
      .type = "com.example.thing",
  };
}

[[nodiscard]] auto minimal_record() -> ce::v1::message {
  ce::v1::message incoming;
  incoming.header_fields.set_exact("ce_specversion", "1.0");
  incoming.header_fields.set_exact("ce_id", "1");
  incoming.header_fields.set_exact("ce_source", "/s");
  incoming.header_fields.set_exact("ce_type", "t");
  return incoming;
}

template <class Codec>
void check_binary_mode(std::string_view codec) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  subject.subject = "s";

  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::binary_mode);
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

  auto read_back = ce::v1::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id == subject.id) << codec;
    expect(bool{read_back->source == subject.source}) << codec;
    expect(read_back->type == subject.type) << codec;
    expect(bool{read_back->subject == subject.subject}) << codec;
  }
}

template <class Codec>
void check_structured_mode(std::string_view codec) {
  using namespace boost::ut;

  const ce::v1::event subject = base_event();

  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::structured);
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

  auto read_back = ce::v1::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id == subject.id) << codec;
  }
}

template <class Codec>
void check_content_type(std::string_view codec) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  subject.datacontenttype = "text/plain";
  subject.data = std::string{"hello"};

  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::binary_mode);
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

  auto read_back = ce::v1::kafka::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(bool{read_back->datacontenttype == subject.datacontenttype}) << codec;
  }
}

template <class Codec>
void check_values_are_unescaped_utf8(std::string_view codec) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  subject.subject = "a b\xC3\xBC";  // a space and U+00FC, both escaped by HTTP

  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::binary_mode);
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

  ce::v1::event subject = base_event();
  subject.subject = std::string{"\xC3"};  // a truncated two-byte sequence

  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::binary_mode);
  expect(!laid_out) << codec;
  if (!laid_out) {
    expect(laid_out.error().code == ce::v1::errc::invalid_utf8) << codec;
    expect(laid_out.error().where == "subject"sv) << codec;
  }
}

template <class Codec>
void check_keys_are_byte_exact(std::string_view codec) {
  using namespace boost::ut;

  // Writing must not erase a differently-cased header the caller set.
  ce::v1::message carrier;
  carrier.header_fields.add("CE_ID", "the caller's own");
  const ce::v1::event subject = base_event();
  auto written = ce::v1::binding::write_attributes<ce::v1::kafka::detail::kafka_traits>(
      subject, carrier.header_fields);
  expect(bool{written}) << codec;
  expect(carrier.header_fields.find_exact("CE_ID") != nullptr) << codec;

  // Reading must not accept a differently-cased prefix as an attribute.
  ce::v1::message incoming = minimal_record();
  incoming.header_fields.add("CE_SOURCE", "/not-the-source");
  auto read_back = ce::v1::kafka::from_message<Codec>(incoming);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(bool{read_back->source == ce::v1::uri_ref{"/s"}}) << codec;
  }
}

template <class Codec>
void check_batch_refused(std::string_view codec) {
  using namespace boost::ut;

  const ce::v1::event subject = base_event();
  auto laid_out = ce::v1::kafka::to_message<Codec>(subject, ce::v1::content_mode::batched);
  expect(!laid_out) << codec;
  if (!laid_out) {
    expect(laid_out.error().code == ce::v1::errc::invalid_argument) << codec;
  }

  // The batch content type also starts with the structured one, so it must be
  // recognised first or this arrives at the format layer as a malformed object.
  ce::v1::message incoming;
  incoming.header_fields.set_exact("content-type", "application/cloudevents-batch+json");
  incoming.body = ce::v1::to_bytes("[]");
  auto read_back = ce::v1::kafka::from_message<Codec>(incoming);
  expect(!read_back) << codec;
  if (!read_back) {
    expect(read_back.error().code == ce::v1::errc::invalid_argument) << codec;
  }
}

template <class Codec>
void check_not_a_cloudevent(std::string_view codec) {
  using namespace boost::ut;

  ce::v1::message incoming;
  incoming.header_fields.set_exact("content-type", "application/json");
  incoming.body = ce::v1::to_bytes("{\"unrelated\":true}");

  auto read_back = ce::v1::kafka::from_message<Codec>(incoming);
  expect(!read_back) << codec;
  if (!read_back) {
    expect(read_back.error().code == ce::v1::errc::not_a_cloudevent) << codec;
  }
}

template <class Codec>
void check_record_and_key_mapper(std::string_view codec) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  expect(bool{subject.set(ce::v1::ext::partitioning{.partitionkey = "customer-42"})}) << codec;
  const ce::v1::event before = subject;

  // Not naming a mapper is the specification's default behaviour.
  auto plain = ce::v1::kafka::to_record<Codec>(subject, ce::v1::content_mode::binary_mode);
  expect(bool{plain}) << codec;
  if (plain) {
    expect(!plain->key.has_value()) << codec;
  }

  auto keyed = ce::v1::kafka::to_record<Codec, ce::v1::kafka::partitionkey_mapper>(
      subject, ce::v1::content_mode::binary_mode);
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
  auto without = ce::v1::kafka::to_record<Codec, ce::v1::kafka::partitionkey_mapper>(
      base_event(), ce::v1::content_mode::binary_mode);
  expect(bool{without}) << codec;
  if (without) {
    expect(!without->key.has_value()) << codec;
  }
}

}  // namespace

const boost::ut::suite<"kafka-binary-mode"> kafka_binary_mode = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_binary_mode<C>(codec);
      check_structured_mode<C>(codec);
    };
  });
};

const boost::ut::suite<"kafka-content-type"> kafka_content_type = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_content_type<C>(codec); };
  });
};

const boost::ut::suite<"kafka-header-values-are-utf8"> kafka_header_values_are_utf8 = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_values_are_unescaped_utf8<C>(codec);
      check_invalid_utf8_is_refused<C>(codec);
    };
  });
};

const boost::ut::suite<"kafka-keys-are-byte-exact"> kafka_keys_are_byte_exact = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_keys_are_byte_exact<C>(codec); };
  });
};

const boost::ut::suite<"kafka-batch-refused"> kafka_batch_refused = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_batch_refused<C>(codec); };
  });
};

const boost::ut::suite<"kafka-not-a-cloudevent"> kafka_not_a_cloudevent = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_not_a_cloudevent<C>(codec); };
  });
};

const boost::ut::suite<"kafka-record-and-key-mapper"> kafka_record_and_key_mapper = [] {
  using namespace boost::ut;

  static_assert(ce::v1::kafka::key_mapper<ce::v1::kafka::no_key_mapper>);
  static_assert(ce::v1::kafka::key_mapper<ce::v1::kafka::partitionkey_mapper>);
  static_assert(std::is_aggregate_v<ce::v1::kafka::record>);

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_record_and_key_mapper<C>(codec); };
  });
};

int main() {}
