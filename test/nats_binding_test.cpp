#include <boost/ut.hpp>

#include <cloudevents/binding/nats.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "codecs_under_test.hpp"
#include "equality.hpp"
#include "mini_codec.hpp"
#include "payload.hpp"

namespace {

using namespace std::string_view_literals;

using namespace ce::literals;

[[nodiscard]] auto base_event(ce::event::options rest = {}) -> ce::event {
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

template <class Codec>
void check_round_trip(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({
      .datacontenttype = "application/json"_mediatype,
      .subject = "s"_subject,
      .extensions = {{"alpha"_ext, std::string{"a"}}},
      .data = ce::json_text{.raw = "{\"a\":1}"},
  });

  auto payload = ce::nats::to_payload<Codec>(subject);
  expect(bool{payload}) << codec;
  if (!payload) {
    return;
  }

  auto read_back = ce::nats::from_payload<Codec>(*payload);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
    expect(bool{read_back->source() == subject.source()}) << codec;
    expect(read_back->type() == subject.type()) << codec;
    expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
    expect(read_back->extensions().size() == 1U) << codec;
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

  auto payload = ce::nats::to_payload<Codec>(subject);
  expect(bool{payload}) << codec << ": structured";
  if (payload) {
    auto read_back = ce::nats::from_payload<Codec>(*payload);
    expect(read_back && ce_test::same_json_payload<Codec>(read_back->data(), document_payload))
        << codec << ": structured";
  }

  auto binary = ce::nats::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{binary}) << codec << ": binary";
  if (binary) {
    auto read_back = ce::nats::from_message<Codec>(*binary);
    expect(read_back && std::holds_alternative<ce::json_text>(read_back->data()) &&
           ce_test::same_json_payload<Codec>(read_back->data(), document_payload))
        << codec << ": binary mode reads JSON text";
  }
}

template <class Codec>
void check_payload_is_the_whole_message(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event();
  auto payload = ce::nats::to_payload<Codec>(subject);
  expect(bool{payload}) << codec;
  if (!payload) {
    return;
  }

  // The event format, not a wrapper around it: the payload must start an object
  // and carry specversion, or another SDK's consumer reads nothing.
  expect(payload->starts_with("{")) << codec;
  expect(payload->find("\"specversion\"") != std::string::npos) << codec;
  expect(payload->find("\"com.example.thing\"") != std::string::npos) << codec;

  // A NATS payload is bytes on a subject. Nothing else travels, so there is
  // nowhere for a content type to go and no header map to hand back.
  static_assert(std::is_same_v<decltype(payload), ce::result<std::string>>);
}

template <class Codec>
void check_every_failure_is_a_parse_error(std::string_view codec) {
  using namespace boost::ut;

  // A perfectly good JSON document that is not a CloudEvent. HTTP and Kafka
  // answer not_a_cloudevent here, from a content type or a specversion header.
  // NATS has neither to consult.
  auto unrelated = ce::nats::from_payload<Codec>(R"({"hello":"world"})"sv);
  expect(!unrelated) << codec;
  if (!unrelated) {
    expect(unrelated.error().code != ce::errc::not_a_cloudevent) << codec;
  }

  auto malformed = ce::nats::from_payload<Codec>("{not json"sv);
  expect(!malformed) << codec;
  if (!malformed) {
    expect(malformed.error().code == ce::errc::parse_error) << codec;
  }

  auto empty = ce::nats::from_payload<Codec>(""sv);
  expect(!empty) << codec;
  if (!empty) {
    expect(empty.error().code != ce::errc::not_a_cloudevent) << codec;
  }
}

template <class Codec>
void check_invalid_event_is_refused(std::string_view codec) {
  using namespace boost::ut;

  // A present-but-empty type is refused where a type is made, naming it, so
  // there is no such event for to_payload to be handed.
  const auto empty_type = ce::type::make(""sv);
  expect(!empty_type) << codec;
  if (!empty_type) {
    expect(empty_type.error().where == "type"sv) << codec;
  }

  // What to_payload can still refuse is a payload the encoder cannot write.
  const ce::event broken = base_event({.data = ce::json_text{.raw = "{not json"}});
  expect(!ce::nats::to_payload<Codec>(broken)) << codec;
}


template <class Codec>
void check_binary_mode(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({
      .datacontenttype = "text/plain"_mediatype,
      .subject = "a b"_subject,  // a space, so the percent-encoding rule is exercised
      .data = std::string{"hello"},
  });

  auto out = ce::nats::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{out}) << codec;
  if (!out) {
    return;
  }

  // The prefix is HTTP's, not Kafka's.
  expect(out->header_fields.find("ce-specversion") != nullptr) << codec;
  expect(out->header_fields.find("ce-id") != nullptr) << codec;
  expect(out->header_fields.find("ce_id") == nullptr) << codec;

  // datacontenttype is an attribute here, unlike every other binding, and
  // Content-Type is left to mean "structured".
  const std::string* declared = out->header_fields.find("ce-datacontenttype");
  expect(declared != nullptr) << codec;
  if (declared != nullptr) {
    expect(*declared == "text/plain"sv) << codec;
  }
  expect(out->header_fields.find("Content-Type") == nullptr) << codec;

  // Values are percent-encoded, by the same rule as HTTP.
  const std::string* written = out->header_fields.find("ce-subject");
  expect(written != nullptr) << codec;
  if (written != nullptr) {
    expect(*written == "a%20b"sv) << codec;
  }

  // The data bytes are the payload.
  expect(ce::to_text(out->body) == "hello") << codec;

  auto read_back = ce::nats::from_message<Codec>(*out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
    expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
    expect(ce_test::equal(read_back->datacontenttype(), subject.datacontenttype())) << codec;
  }
}

template <class Codec>
void check_mode_detection(std::string_view codec) {
  using namespace boost::ut;

  // The binding inverts HTTP's default: no content type means BINARY, not
  // structured, so a message with neither is not a CloudEvent rather than an
  // empty structured document.
  const ce::message bare{};
  expect(ce::nats::detect_content_mode(bare) == ce::content_mode::binary_mode) << codec;

  auto refused = ce::nats::from_message<Codec>(bare);
  expect(!refused) << codec;
  if (!refused) {
    expect(refused.error().code == ce::errc::not_a_cloudevent) << codec;
  }

  const ce::message structured{
      .header_fields = {{"content-type", "application/cloudevents+json; charset=utf-8"}}};
  expect(ce::nats::detect_content_mode(structured) == ce::content_mode::structured) << codec;

  const ce::event subject = base_event();
  auto encoded = ce::nats::to_message<Codec>(subject, ce::content_mode::structured);
  expect(bool{encoded}) << codec;
  if (encoded) {
    expect(encoded->header_fields.find("Content-Type") != nullptr) << codec;
    auto read_back = ce::nats::from_message<Codec>(*encoded);
    expect(bool{read_back}) << codec;
    if (read_back) {
      expect(read_back->id() == subject.id()) << codec;
    }
  }

  auto batched = ce::nats::to_message<Codec>(subject, ce::content_mode::batched);
  expect(!batched) << codec;
  if (!batched) {
    expect(batched.error().code == ce::errc::invalid_argument) << codec;
  }
}

}  // namespace

// spec: SWR-NATS-0001
const boost::ut::suite<"nats-structured-only"> nats_structured_only = [] {
  using namespace boost::ut;

  // No content mode parameter, and no way to ask for one: the transport has no
  // headers, so binary mode does not exist here.
  static_assert(std::is_invocable_v<decltype(&ce::nats::to_payload<ce::test::mini_codec>),
                                    const ce::event&>);

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_round_trip<C>(codec);
      check_document_round_trip<C>(codec);
      check_invalid_event_is_refused<C>(codec);
    };
  });
};

// spec: SWR-NATS-0002
// spec: SWR-NATS-0003
const boost::ut::suite<"nats-payload-is-the-whole-message"> nats_payload_is_the_whole_message = [] {
  using namespace boost::ut;

  // The subject is the application's. The specification defines no derivation,
  // so there is no parameter to pass one through and none to be derived into.
  static_assert(!std::is_invocable_v<decltype(&ce::nats::to_payload<ce::test::mini_codec>),
                                     const ce::event&, std::string_view>);

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_payload_is_the_whole_message<C>(codec); };
  });
};

// spec: SWR-NATS-0004
const boost::ut::suite<"nats-every-failure-is-a-parse-error"> nats_every_failure_is_a_parse_error =
    [] {
      using namespace boost::ut;
      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] { check_every_failure_is_a_parse_error<C>(codec); };
      });
    };


// spec: SWR-NATS-0001
const boost::ut::suite<"nats-binary-mode"> nats_binary_mode = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_binary_mode<C>(codec);
      check_mode_detection<C>(codec);
    };
  });
};

int main() {}
