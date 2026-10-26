/// \file
/// \brief Fuzz MQTT message decoding.
///
/// The input is a Content Type property and User Properties as `name: value` lines, a blank line,
/// then the payload, so one input drives the 5.0 entry point with any header list and the 3.1.1
/// entry points with the same bytes (SWR-MQTT-0016). Binary mode is reached under both name
/// policies, so the input steers the attribute filter, the datacontenttype copy and the value
/// rules. Not a duplicate of fuzz_kafka_message: this binding compares the content-type name byte
/// for byte, refuses a second header of that name, refuses a batch and ignores foreign User
/// Properties, none of which the Kafka target reaches.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <cloudevents/binding/mqtt.hpp>
#include <cloudevents/codec/nlohmann.hpp>

namespace {

using codec = ce::codec::nlohmann_codec;

/// Consume `name:value` lines until a blank line; the rest is the payload.
auto build(std::string_view input) -> ce::message {
  ce::message incoming;
  std::size_t pos = 0;
  while (pos < input.size()) {
    const std::size_t line_end = input.find('\n', pos);
    const std::string_view line =
        input.substr(pos, line_end == std::string_view::npos ? input.size() - pos : line_end - pos);
    if (line.empty()) {
      pos = line_end == std::string_view::npos ? input.size() : line_end + 1;
      break;
    }
    if (const std::size_t colon = line.find(':'); colon != std::string_view::npos) {
      incoming.header_fields.add(std::string{line.substr(0, colon)},
                                 std::string{line.substr(colon + 1)});
    }
    if (line_end == std::string_view::npos) {
      pos = input.size();
      break;
    }
    pos = line_end + 1;
  }
  for (const char character : input.substr(pos)) {
    incoming.body.push_back(static_cast<std::byte>(character));
  }
  return incoming;
}

/// Reads `incoming` under one name policy and checks what the binding promises of the result.
template<class Names>
void exercise(const ce::message& incoming) {
  // The text path above the retention limit takes the same input (SWR-BIND-0007).
  (void)ce::mqtt::from_message<codec, Names>(incoming, {.retain_document_up_to = 0});

  auto decoded = ce::mqtt::from_message<codec, Names>(incoming);
  if (!decoded) {
    return;
  }

  if (ce::mqtt::detect_content_mode(incoming) == ce::content_mode::structured) {
    // Whatever a structured 5.0 message decoded to is what its payload alone decodes to on 3.1.1.
    auto from_payload = ce::mqtt::from_payload<codec>(incoming);
    if (!from_payload || !(*decoded == *from_payload)) {
      __builtin_trap();
    }
  }

  // A decoded event writes back out in structured mode, and in binary mode when its values fit an
  // MQTT string; what binary mode wrote reads back under the same policy.
  if (!ce::mqtt::to_message<codec, Names>(*decoded, ce::content_mode::structured)) {
    __builtin_trap();
  }
  if (auto written = ce::mqtt::to_message<codec, Names>(*decoded, ce::content_mode::binary_mode);
      written && !ce::mqtt::from_message<codec, Names>(*written)) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view input{reinterpret_cast<const char*>(data), size};
  const ce::message incoming = build(input);

  (void)ce::mqtt::detect_content_mode(incoming);
  (void)ce::mqtt::from_payload<codec>(incoming, {.retain_document_up_to = 0});

  exercise<ce::mqtt::unprefixed_names>(incoming);
  exercise<ce::mqtt::prefixed_names>(incoming);
  return 0;
}
