/// \file
/// \brief Fuzz Kafka record decoding.
///
/// Not a duplicate of fuzz_http_message. The two bindings take opposite branches
/// of every `if constexpr` in the shared core: this one drives byte-exact key
/// matching, the `ce_` prefix, unescaped values and UTF-8 validation on the way
/// in, none of which the HTTP target reaches.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <cloudevents/v1/binding/kafka.hpp>
#include <cloudevents/codec/nlohmann.hpp>

namespace {

/// Consume `name: value` lines until a blank line; the rest is the record value.
auto build(std::string_view input) -> ce::v1::message {
  ce::v1::message incoming;
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
  const ce::v1::message incoming = build(input);

  (void)ce::v1::kafka::detect_content_mode(incoming);
  auto decoded = ce::v1::kafka::from_message<ce::v1::codec::nlohmann_codec>(incoming);
  if (decoded) {
    // Every header value that got here passed UTF-8 validation on the way in, so
    // writing it back out cannot fail on UTF-8; a failure means the two
    // directions disagree about what a valid value is.
    if (!ce::v1::kafka::to_message<ce::v1::codec::nlohmann_codec>(*decoded,
                                                          ce::v1::content_mode::binary_mode)) {
      __builtin_trap();
    }

    // Key mapping reads the event and never edits it (SWR-KAFKA-0009).
    const ce::v1::event before = *decoded;
    auto keyed = ce::v1::kafka::to_record<ce::v1::codec::nlohmann_codec, ce::v1::kafka::partitionkey_mapper>(
        *decoded, ce::v1::content_mode::binary_mode);
    if (!keyed || !(*decoded == before)) {
      __builtin_trap();
    }
    // A key implies the attribute that produced it still travels.
    if (keyed->key && keyed->value.header_fields.find_exact("ce_partitionkey") == nullptr) {
      __builtin_trap();
    }
  }
  return 0;
}
