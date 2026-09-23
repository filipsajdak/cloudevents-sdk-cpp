/// \file
/// \brief Fuzz both NATS decode paths.
///
/// The payload path is the same code fuzz_json_decode drives, so it earns its
/// place on a stronger property rather than on new coverage: decode, encode and
/// decode again must reach the same event. fuzz_json_decode only asserts that
/// re-encoding succeeds, which a format that lost an attribute would also
/// satisfy.
///
/// The message path is new coverage. Binary mode percent-decodes header values
/// and reads datacontenttype from a prefixed header, neither of which any other
/// fuzz target reaches with this binding's traits.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cloudevents/v2/binding/nats.hpp>
#include <cloudevents/codec/nlohmann.hpp>

#include <string>

namespace {

/// Consume `name: value` lines until a blank line; the rest is the payload.
auto build(std::string_view input) -> ce::v2::message {
  ce::v2::message incoming;
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
  const std::string_view payload{reinterpret_cast<const char*>(data), size};

  const ce::v2::message incoming = build(payload);
  (void)ce::v2::nats::detect_content_mode(incoming);
  if (auto from_headers = ce::v2::nats::from_message<ce::v2::codec::nlohmann_codec>(incoming)) {
    // Everything that got here percent-decoded and validated, so writing it back
    // out cannot fail; a failure means the two directions disagree.
    if (!ce::v2::nats::to_message<ce::v2::codec::nlohmann_codec>(*from_headers,
                                                         ce::v2::content_mode::binary_mode)) {
      __builtin_trap();
    }
  }

  auto decoded = ce::v2::nats::from_payload<ce::v2::codec::nlohmann_codec>(payload);
  if (!decoded) {
    return 0;
  }

  auto reserialized = ce::v2::nats::to_payload<ce::v2::codec::nlohmann_codec>(*decoded);
  if (!reserialized) {
    __builtin_trap();
  }

  auto again = ce::v2::nats::from_payload<ce::v2::codec::nlohmann_codec>(*reserialized);
  if (!again || !(*again == *decoded)) {
    __builtin_trap();
  }
  return 0;
}
