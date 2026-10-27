#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/binding/nats.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "binding_decode_options.hpp"
#include "codecs_under_test.hpp"
#include "equality.hpp"
#include "mini_codec.hpp"
#include "moving_entry_points.hpp"
#include "payload.hpp"
#include "protobuf_binding_checks.hpp"

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
    expect(read_back && std::holds_alternative<ce::json_document>(read_back->data()) &&
           ce_test::same_json_payload<Codec>(read_back->data(), document_payload))
        << codec << ": binary mode reads a JSON document";
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

// spec: SWR-BIND-0009
const boost::ut::suite<"from-message-takes-decode-options"> nats_from_message_takes_options = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::check_from_message_takes_decode_options<C>(
          codec,
          [](const ce::event& subject, ce::content_mode mode) {
            return ce::nats::to_message<C>(subject, mode);
          },
          [](const ce::message& laid_out, ce::json::decode_options options) {
            return ce::nats::from_message<C>(laid_out, options);
          });
    };
  });
};

// spec: SWR-BIND-0009
const boost::ut::suite<"from-payload-takes-decode-options"> nats_from_payload_takes_options = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      const auto payload = ce::nats::to_payload<C>(ce_test::options_event());
      expect(payload.has_value()) << codec;
      if (!payload) {
        return;
      }
      const auto is_document = [](const ce::result<ce::event>& read) {
        return read.has_value() && std::holds_alternative<ce::json_document>(read->data());
      };
      const auto is_text = [](const ce::result<ce::event>& read) {
        return read.has_value() && std::holds_alternative<ce::json_text>(read->data());
      };
      expect(is_document(ce::nats::from_payload<C>(*payload))) << codec;
      expect(is_document(ce::nats::from_payload<C>(*payload, {}))) << codec;
      expect(is_document(
          ce::nats::from_payload<C>(*payload, {.retain_document_up_to = payload->size()})))
          << codec;
      expect(is_text(
          ce::nats::from_payload<C>(*payload, {.retain_document_up_to = payload->size() - 1U})))
          << codec;
      expect(is_text(ce::nats::from_payload<C>(*payload, {.retain_document_up_to = 0}))) << codec;
    };
  });
};

// spec: SWR-BIND-0008
// spec: SWR-BIND-0010
const boost::ut::suite<"nats-binary-mode-json-body"> nats_binary_mode_json_body = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce::message incoming{.header_fields = {{"ce-specversion", "1.0"},
                                             {"ce-id", "1"},
                                             {"ce-source", "/s"},
                                             {"ce-type", "t"},
                                             {"ce-datacontenttype", "application/json"}}};
      incoming.body = ce::to_bytes("{not json");
      const auto refused = ce::nats::from_message<C>(incoming);
      expect(!refused.has_value()) << codec;
      if (!refused) {
        expect(refused.error().code == ce::errc::parse_error) << codec;
      }
      incoming.body = {};
      const auto empty = ce::nats::from_message<C>(incoming);
      expect(empty.has_value() && std::holds_alternative<std::monostate>(empty->data())) << codec;
    };
  });
};

// spec: SWR-BIND-0012
const boost::ut::suite<"nats-binary-body-not-parsed"> nats_binary_body_not_parsed = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::check_binary_body_not_parsed<C>(
          codec,
          [](const ce::event& subject, ce::content_mode mode) {
            return ce::nats::to_message<C>(subject, mode);
          },
          [](const ce::message& laid_out, ce::json::decode_options options) {
            return ce::nats::from_message<C>(laid_out, options);
          });
    };
  });
};

// spec: SWR-BIND-0013
const boost::ut::suite<"nats-from-message-ignores-parse-binary-body-in-structured-mode">
    nats_structured_ignores = [] {
      using namespace boost::ut;

      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] {
          ce_test::check_structured_ignores_parse_binary_body<C>(
              codec,
              [](const ce::event& subject, ce::content_mode mode) {
                return ce::nats::to_message<C>(subject, mode);
              },
              [](const ce::message& laid_out, ce::json::decode_options options) {
                return ce::nats::from_message<C>(laid_out, options);
              });
        };
      });
    };

// spec: SWR-BIND-0013
const boost::ut::suite<"from-payload-ignores-parse-binary-body"> nats_payload_ignores = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      const auto payload = ce::nats::to_payload<C>(ce_test::options_event());
      expect(payload.has_value()) << codec;
      if (!payload) {
        return;
      }
      for (const bool parse : {true, false}) {
        const auto read = ce::nats::from_payload<C>(*payload, {.parse_binary_body = parse});
        expect(read.has_value() && std::holds_alternative<ce::json_document>(read->data()))
            << codec;
      }
      constexpr std::string_view malformed = "{not json";
      const auto refused = ce::nats::from_payload<C>(malformed, {.parse_binary_body = false});
      expect(!refused.has_value()) << codec << ": a malformed payload still fails";
      if (!refused) {
        expect(refused.error().code == ce::errc::parse_error) << codec;
      }
    };
  });
};

// --- CR-0005: the moving entry points -----------------------------------------

namespace {

using mini_codec = ce::test::mini_codec;

/// The NATS entry points under the shared checks of moving_entry_points.hpp. A NATS message
/// carries its media type as the `ce-datacontenttype` attribute.
template<class C>
struct nats_under_test {
  static constexpr std::string_view prefix = "ce-";
  static constexpr std::string_view media_header = "ce-datacontenttype";

  [[nodiscard]] static auto lay_out(const ce::event& subject, ce::content_mode mode)
      -> ce::result<ce::message> {
    return ce::nats::to_message<C>(subject, mode);
  }

  template<class Message>
  [[nodiscard]] static auto copying(Message&& given, ce::json::decode_options options)
      -> ce::result<ce::event> {
    return ce::nats::from_message<C>(std::forward<Message>(given), options);
  }

  [[nodiscard]] static auto moving(ce::message&& given, ce::json::decode_options options)
      -> ce::result<ce::event> {
    return ce::nats::from_message_moving<C>(std::move(given), options);
  }
};

template<class M>
constexpr bool nats_moving_accepts =
    requires(M&& given) { ce::nats::from_message_moving<mini_codec>(std::forward<M>(given)); };
}  // namespace

// spec: SWR-NATS-0005
// spec: SWR-BIND-0014
const boost::ut::suite<"nats-from-message-moving-agrees-with-from-message"> nats_moving_agrees =
    [] {
      using namespace boost::ut;

      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] {
          ce_test::moving::check_moving_agrees_with_copying<nats_under_test<C>>(codec);
        };
      });

      "the entry point returns a result of an event"_test = [] {
        static_assert(std::is_same_v<decltype(ce::nats::from_message_moving<mini_codec>(
                                         std::declval<ce::message>())),
                                     ce::result<ce::event>>);
        static_assert(nats_moving_accepts<ce::message>);
        static_assert(!nats_moving_accepts<ce::message&>);
        static_assert(!nats_moving_accepts<const ce::message&>);
      };
    };

// spec: SWR-BIND-0014
// spec: SWR-BIND-0015
const boost::ut::suite<"nats-moving-call-releases-a-moved-body"> nats_moving_releases = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::moving::check_moved_body_is_released<nats_under_test<C>>(codec);
    };
  });
};

// spec: SWR-BIND-0015
const boost::ut::suite<"nats-moving-call-leaves-an-unmoved-body-intact"> nats_moving_unmoved = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::moving::check_unmoved_body_is_left_intact<nats_under_test<C>>(codec);
    };
  });
};

// spec: SWR-BIND-0016
const boost::ut::suite<"nats-moving-call-failure-keeps-the-message"> nats_moving_failure = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::moving::check_failed_call_leaves_the_message<nats_under_test<C>>(codec);
    };
  });
};

// spec: SWR-BIND-0017
const boost::ut::suite<"nats-from-message-leaves-the-message-intact"> nats_copying_intact = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce_test::moving::check_copying_entry_point_leaves_the_message<nats_under_test<C>>(codec);
    };
  });
};

namespace {

template<class S>
constexpr bool nats_payload_moving_accepts =
    requires(S&& given) { ce::nats::from_payload_moving<mini_codec>(std::forward<S>(given)); };

}  // namespace

// spec: SWR-NATS-0006
// spec: SWR-BIND-0018
const boost::ut::suite<"payload-moving-agrees-with-the-copying-payload"> nats_payload_agrees = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      static_assert(nats_payload_moving_accepts<std::string>);
      static_assert(!nats_payload_moving_accepts<std::string&>);
      static_assert(!nats_payload_moving_accepts<std::string_view>);
      static_assert(
          std::is_same_v<decltype(ce::nats::from_payload_moving<C>(std::declval<std::string>())),
                         ce::result<ce::event>>);
      const auto payload = ce::nats::to_payload<C>(ce_test::options_event());
      expect(payload.has_value()) << codec;
      if (!payload) {
        return;
      }
      for (const auto& options : {ce::json::decode_options{},
                                  ce::json::decode_options{.retain_document_up_to = 0},
                                  ce::json::decode_options{.parse_binary_body = false}}) {
        std::string given = *payload;
        const auto by_copy = ce::nats::from_payload<C>(*payload, options);
        const auto by_move = ce::nats::from_payload_moving<C>(std::move(given), options);
        expect(ce_test::moving::agree(by_copy, by_move)) << codec;
        expect(by_move.has_value()) << codec;
      }

      constexpr std::string_view malformed = "{not json";
      const auto refused_by_copy = ce::nats::from_payload<C>(malformed);
      const auto refused_by_move = ce::nats::from_payload_moving<C>(std::string{malformed});
      expect(!refused_by_copy.has_value() && !refused_by_move.has_value()) << codec;
      if (!refused_by_copy && !refused_by_move) {
        expect(refused_by_copy.error().code == refused_by_move.error().code) << codec;
      }
    };
  });
};

// spec: SWR-NATS-0006
// spec: SWR-BIND-0018
const boost::ut::suite<"payload-moving-never-modifies-the-string"> nats_payload_intact = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      const auto leaves = [&codec](std::string given, std::string_view what) {
        const std::string before = given;
        const char* const address = given.data();
        const std::size_t capacity = given.capacity();
        const auto read = ce::nats::from_payload_moving<C>(std::move(given));
        (void)read;
        // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
        expect(given == before && given.data() == address && given.capacity() == capacity)
            << codec << ": " << what;
        // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
      };
      const auto payload = ce::nats::to_payload<C>(ce_test::options_event());
      expect(payload.has_value()) << codec;
      if (!payload) {
        return;
      }
      leaves(*payload, "a payload");
      leaves(std::string{ce_test::moving::malformed_json_body}, "a malformed payload");
      leaves(std::string{}, "an empty payload");
      std::string large = *payload;
      large.reserve(ce_test::moving::large_body_bytes);
      leaves(std::move(large), "a payload in a larger buffer");
    };
  });
};

namespace {

/// The NATS binding's Protobuf message entry points, as protobuf_binding_checks.hpp drives them.
struct nats_protobuf {
  static constexpr std::string_view label = "nats";
  static constexpr std::string_view header = "Content-Type";
  static constexpr bool names_ignore_case = true;

  static auto lay_out(const ce::event& cloud_event) -> ce::result<ce::message> {
    return ce::nats::to_message_protobuf(cloud_event);
  }

  static auto read(const ce::message& incoming, ce::protobuf::decode_options options)
      -> ce::result<ce::event> {
    return ce::nats::from_message_protobuf(incoming, options);
  }
};

// None is a template, and the options default: a pointer to each names one function.
static_assert(std::is_same_v<decltype(&ce::nats::to_message_protobuf),
                             ce::result<ce::message> (*)(const ce::event&)>);
static_assert(
    std::is_same_v<decltype(&ce::nats::from_message_protobuf),
                   ce::result<ce::event> (*)(const ce::message&, ce::protobuf::decode_options)>);
static_assert(std::is_same_v<decltype(&ce::nats::to_payload_protobuf),
                             ce::result<ce::binary> (*)(const ce::event&)>);
static_assert(std::is_same_v<decltype(&ce::nats::from_payload_protobuf),
                             ce::result<ce::event> (*)(std::span<const std::byte>,
                                                       ce::protobuf::decode_options)>);

/// The payload and the decoder disagree: true when `from_payload_protobuf` and
/// `protobuf_format::decode` give the same result for `bytes`.
[[nodiscard]] auto reads_as_the_decoder_does(const ce::binary& bytes) -> bool {
  const auto by_payload = ce::nats::from_payload_protobuf(bytes);
  const auto by_decoder = ce::protobuf_format::decode(bytes);
  if (by_payload.has_value() != by_decoder.has_value()) {
    return false;
  }
  return by_payload ? bool{*by_payload == *by_decoder}
                    : by_payload.error().code == by_decoder.error().code;
}

}  // namespace

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-to-message-protobuf"> nats_to_message_protobuf = [] {
  using namespace boost::ut::literals;
  "one Content-Type header and the bytes of protobuf_format::encode"_test = [] {
    ce_test::protobuf_binding::lays_out_one_header_and_the_encoded_bytes<nats_protobuf>();
  };
};

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-from-message-protobuf"> nats_from_message_protobuf = [] {
  using namespace boost::ut::literals;
  "a message it laid out is read back to the event"_test = [] {
    ce_test::protobuf_binding::reads_what_it_lays_out<nats_protobuf>();
  };
  "a message another writer built is read"_test = [] {
    ce_test::protobuf_binding::reads_a_message_another_writer_built<nats_protobuf>();
  };
  "the decode limits are passed through"_test = [] {
    ce_test::protobuf_binding::passes_the_decode_limits_through<nats_protobuf>();
  };
  "a bad body is the error of the decoder"_test = [] {
    ce_test::protobuf_binding::gives_the_decoder_error_for_a_bad_body<nats_protobuf>();
  };
  "a message given with std::move is read and left as it was"_test = [] {
    ce_test::protobuf_binding::leaves_a_moved_message_alone<nats_protobuf>();
  };
};

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-from-message-protobuf-refuses-other-content-type"> nats_refuses = [] {
  using namespace boost::ut::literals;
  "any other media type, or none, is not_a_cloudevent"_test = [] {
    ce_test::protobuf_binding::refuses_another_content_type<nats_protobuf>();
  };
  "the media type is compared ignoring case and parameters"_test = [] {
    ce_test::protobuf_binding::ignores_case_and_parameters<nats_protobuf>();
  };
  "the header name is found ignoring case"_test = [] {
    ce_test::protobuf_binding::honours_the_binding_rule_for_header_name_case<nats_protobuf>();
  };
};

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-to-payload-protobuf"> nats_to_payload_protobuf = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;
  "the payload is the bytes of protobuf_format::encode, with nothing around them"_test = [] {
    for (const auto& subject : ce_test::protobuf_binding::samples()) {
      const auto payload = ce::nats::to_payload_protobuf(subject);
      const auto encoded = ce::protobuf_format::encode(subject);
      expect(payload.has_value() && encoded.has_value());
      if (payload && encoded) {
        expect(*payload == *encoded);
      }
    }
  };
  "the payload is the body of the message form"_test = [] {
    const auto subject = ce::test::pb::base_event();
    const auto payload = ce::nats::to_payload_protobuf(subject);
    const auto message = ce::nats::to_message_protobuf(subject);
    expect(payload.has_value() && message.has_value());
    if (payload && message) {
      expect(*payload == message->body);
    }
  };
};

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-from-payload-protobuf-always-reads-protobuf"> nats_payload_reads = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;
  "a payload it wrote is read back to the event"_test = [] {
    for (const auto& subject : ce_test::protobuf_binding::samples()) {
      const auto payload = ce::nats::to_payload_protobuf(subject);
      expect(payload.has_value());
      if (!payload) {
        continue;
      }
      const auto read = ce::nats::from_payload_protobuf(*payload);
      expect(read.has_value() && bool{*read == subject});
    }
  };
  "bytes another writer built are read, with no content type to tell"_test = [] {
    const auto read = ce::nats::from_payload_protobuf(ce::test::pb::event_bytes());
    expect(read.has_value() && bool{*read == ce::test::pb::base_event()});
  };
  "the decode limits are passed through"_test = [] {
    const ce::binary bytes = ce::test::pb::event_bytes();
    expect(ce::nats::from_payload_protobuf(bytes, {.max_encoded_size = bytes.size()}).has_value());
    const auto refused = ce::nats::from_payload_protobuf(bytes, {.max_encoded_size = 0});
    expect(!refused.has_value() && refused.error().code == ce::errc::out_of_range);
  };
  "any bytes are read as Protobuf, JSON text included"_test = [] {
    const auto json = ce::to_bytes(R"({"specversion":"1.0","id":"1","source":"/s","type":"t"})");
    expect(reads_as_the_decoder_does(json));
    expect(!ce::nats::from_payload_protobuf(json).has_value());
    // The JSON entry point reads the same bytes as JSON: the name decides, not the bytes.
    expect(ce::nats::from_payload<ce::test::mini_codec>(ce::to_text(json)).has_value());
  };
};

// spec: SWR-PROTO-0021
const boost::ut::suite<"nats-from-payload-protobuf-garbage-is-parse-error"> nats_payload_garbage =
    [] {
      using boost::ut::expect;
      using namespace boost::ut::literals;
      "bytes that are not an event are parse_error"_test = [] {
        const ce::binary garbage(ce_test::protobuf_binding::garbage_bytes, std::byte{0xFF});
        const auto read = ce::nats::from_payload_protobuf(garbage);
        expect(!read.has_value() && read.error().code == ce::errc::parse_error);
      };
      "no payload at all is the decoder's answer for an empty message"_test = [] {
        expect(reads_as_the_decoder_does(ce::binary{}));
        expect(!ce::nats::from_payload_protobuf(ce::binary{}).has_value());
      };
    };

int main() {}
