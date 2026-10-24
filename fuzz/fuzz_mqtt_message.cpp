/// \file
/// \brief Fuzz MQTT message decoding.
///
/// The input is a Content Type property and User Properties as `name: value` lines, a blank line,
/// then the payload, so one input drives the 5.0 entry point with any header list and the 3.1.1
/// entry points with the same bytes (SWR-MQTT-0016). Not a duplicate of fuzz_kafka_message: this
/// binding compares the content-type name byte for byte, refuses a second header of that name and
/// refuses a batch, none of which the Kafka target reaches.

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

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view input{reinterpret_cast<const char*>(data), size};
  const ce::message incoming = build(input);

  (void)ce::mqtt::detect_content_mode(incoming);
  // The text path above the retention limit takes the same input (SWR-BIND-0007).
  (void)ce::mqtt::from_message<codec>(incoming, {.retain_document_up_to = 0});
  (void)ce::mqtt::from_payload<codec>(incoming, {.retain_document_up_to = 0});

  auto decoded = ce::mqtt::from_message<codec>(incoming);
  if (decoded) {
    // Whatever a 5.0 message decoded to is what its payload alone decodes to on 3.1.1.
    auto from_payload = ce::mqtt::from_payload<codec>(incoming);
    if (!from_payload || !(*decoded == *from_payload)) {
      __builtin_trap();
    }

    // A decoded event writes back out, on 5.0 and as a 3.1.1 payload.
    if (!ce::mqtt::to_message<codec>(*decoded, ce::content_mode::structured) ||
        !ce::mqtt::to_payload<codec>(*decoded)) {
      __builtin_trap();
    }
  }
  return 0;
}
