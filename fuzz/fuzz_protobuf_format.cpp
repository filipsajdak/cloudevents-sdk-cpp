/// \file
/// \brief Fuzz the Protobuf event decoder, and the binding entry points around it.
///
/// A hand-written parser of untrusted bytes: lengths, varints, a map, an opaque Any
/// (SWR-PROTO-0017, SWR-SEC-0001). Whatever it accepts must re-encode, and the encoding must
/// decode to the same event (SWR-PROTO-0012). The HTTP, Kafka and NATS entry points hand the same
/// bytes to the same decoder, so each must agree with it, under any spelling of the content type
/// and under the same limits (SWR-PROTO-0019 to -0021).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/binding/nats.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace {

using format = ce::protobuf_format;

/// Limits tight enough that a short input reaches every limit check.
constexpr ce::protobuf::decode_options tight{
    .max_encoded_size = 64,
    .max_attribute_count = 2,
    .max_batch_event_count = 2,
};

/// An event that decoded must re-encode, deterministically, and decode again to an equal event.
void check_event_round_trip(const ce::event& decoded) {
  const auto encoded = format::encode(decoded);
  if (!encoded) {
    __builtin_trap();
  }
  const auto again = format::decode(*encoded);
  if (!again || !(*again == decoded)) {
    __builtin_trap();
  }
  const auto repeated = format::encode(decoded);
  if (!repeated || *repeated != *encoded) {
    __builtin_trap();
  }
}

void check_batch_round_trip(std::span<const ce::event> events) {
  const auto encoded = format::encode_batch(events);
  if (!encoded) {
    __builtin_trap();
  }
  const auto again = format::decode_batch(*encoded);
  if (!again || !(*again == std::vector<ce::event>{events.begin(), events.end()})) {
    __builtin_trap();
  }
}

/// A decode under tight limits either fails or gives what the default limits give.
void check_limits_agree(std::span<const std::byte> input, const ce::result<ce::event>& unlimited) {
  const auto limited = format::decode(input, tight);
  if (limited && (!unlimited || !(*limited == *unlimited))) {
    __builtin_trap();
  }
}

/// The same decision: both fail with one code, or both give equal events.
[[nodiscard]] auto agree(const ce::result<ce::event>& left, const ce::result<ce::event>& right)
    -> bool {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  return left ? *left == *right : left.error().code == right.error().code;
}

/// Spellings of the media type that every entry point reads as application/cloudevents+protobuf.
constexpr std::array<std::string_view, 3> accepted_spellings{
    "application/cloudevents+protobuf",
    "APPLICATION/CloudEvents+Protobuf; charset=utf-8",
    " \tapplication/cloudevents+protobuf ;a=b",
};

/// Media types that none reads: a prefix, an extension of the name, the batch type, and none.
constexpr std::array<std::string_view, 4> refused_spellings{
    "application/cloudevents+protobufx",
    "application/cloudevents-batch+protobuf",
    "application/cloudevents+json",
    "",
};

[[nodiscard]] auto message_of(std::string_view header,
                              std::string_view type,
                              std::span<const std::byte> body) -> ce::message {
  return ce::message{.header_fields = {{std::string{header}, std::string{type}}},
                     .body = ce::binary{body.begin(), body.end()}};
}

void trap_unless(bool holds) {
  if (!holds) {
    __builtin_trap();
  }
}

/// Each binding reads a structured message exactly as the format reads its body, and refuses a
/// message whose media type is another one; the NATS payload form reads any bytes as Protobuf.
void check_bindings_agree(std::span<const std::byte> input) {
  const auto plain = format::decode(input);
  const auto limited = format::decode(input, tight);
  const auto spelled = accepted_spellings[input.size() % accepted_spellings.size()];

  const auto over_http = message_of("content-TYPE", spelled, input);
  const auto over_kafka = message_of("content-type", spelled, input);
  const auto over_nats = message_of("Content-Type", spelled, input);
  trap_unless(agree(ce::http::from_message_protobuf(over_http), plain));
  trap_unless(agree(ce::kafka::from_message_protobuf(over_kafka), plain));
  trap_unless(agree(ce::nats::from_message_protobuf(over_nats), plain));
  trap_unless(agree(ce::nats::from_payload_protobuf(input), plain));
  trap_unless(agree(ce::http::from_message_protobuf(over_http, tight), limited));
  trap_unless(agree(ce::kafka::from_message_protobuf(over_kafka, tight), limited));
  trap_unless(agree(ce::nats::from_message_protobuf(over_nats, tight), limited));
  trap_unless(agree(ce::nats::from_payload_protobuf(input, tight), limited));

  for (const auto other : refused_spellings) {
    const auto code = ce::errc::not_a_cloudevent;
    const auto http_read =
        ce::http::from_message_protobuf(message_of("Content-Type", other, input));
    const auto kafka_read =
        ce::kafka::from_message_protobuf(message_of("content-type", other, input));
    const auto nats_read =
        ce::nats::from_message_protobuf(message_of("Content-Type", other, input));
    trap_unless(!http_read && http_read.error().code == code);
    trap_unless(!kafka_read && kafka_read.error().code == code);
    trap_unless(!nats_read && nats_read.error().code == code);
  }

  // What was read is laid out by every binding and read back to the same event.
  if (plain) {
    const auto http_back = ce::http::from_message_protobuf(*ce::http::to_message_protobuf(*plain));
    const auto kafka_back =
        ce::kafka::from_message_protobuf(*ce::kafka::to_message_protobuf(*plain));
    const auto nats_back = ce::nats::from_message_protobuf(*ce::nats::to_message_protobuf(*plain));
    const auto payload_back =
        ce::nats::from_payload_protobuf(*ce::nats::to_payload_protobuf(*plain));
    trap_unless(agree(http_back, plain) && agree(kafka_back, plain) && agree(nats_back, plain) &&
                agree(payload_back, plain));
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::span<const std::byte> input{reinterpret_cast<const std::byte*>(data), size};

  const auto decoded = format::decode(input);
  if (decoded) {
    check_event_round_trip(*decoded);
  }
  check_limits_agree(input, decoded);

  // The side channel reads the same bytes, so it agrees with the plain decode.
  const auto detailed = format::decode_detailed(input);
  if (detailed.has_value() != decoded.has_value() || (detailed && !(detailed->event == *decoded))) {
    __builtin_trap();
  }

  const auto batch = format::decode_batch(input);
  if (batch) {
    check_batch_round_trip(*batch);
  }
  (void)format::decode_batch(input, tight);

  check_bindings_agree(input);
  return 0;
}
