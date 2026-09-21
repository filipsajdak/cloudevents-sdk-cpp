#pragma once

/// \file
/// \brief The CloudEvents NATS protocol binding.
///
/// The smallest binding in the SDK, and the two facts a caller will get wrong are
/// both here rather than in a specification they have not read.
///
/// **There is no binary mode, and no choice of event format.** The binding
/// specification says NATS "will only support _structured_ data mode at this
/// time", because "the NATS protocol does not support custom message headers,
/// necessary for _binary_ mode", and that every implementation "MUST support the
/// JSON event format". So there is no content mode parameter and no header map:
/// the payload is the JSON event and nothing else. The codec stays a template
/// parameter because it chooses the JSON library, not the format.
///
/// **The subject is yours.** The specification defines no mapping from an event
/// to a NATS subject, so this binding does not invent one. Publishing to a
/// subject is the application's, exactly as issuing an HTTP request is.

#include <string>
#include <string_view>

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::nats {

/// \brief The NATS message payload for an event: UTF-8 JSON text.
///
/// `ce::to_bytes` converts it where a client wants bytes.
template <json::json_codec Codec>
[[nodiscard]] auto to_payload(const event& subject) -> result<std::string> {
  return json_format<Codec>::encode(subject);
}

/// \brief Read an event from a NATS message payload.
///
/// **A payload that is not a CloudEvent cannot be told from a malformed one.**
/// The HTTP and Kafka bindings answer `errc::not_a_cloudevent` because they have
/// a content type or a `ce_specversion` header to consult; a NATS payload carries
/// neither, so an unrelated JSON document is indistinguishable from a corrupt
/// event and every failure here is a parse or validation error. A caller who
/// needs the distinction has to carry it in the subject.
template <json::json_codec Codec>
[[nodiscard]] auto from_payload(std::string_view payload) -> result<event> {
  return json_format<Codec>::decode(payload);
}

}  // namespace ce::inline v1::nats
