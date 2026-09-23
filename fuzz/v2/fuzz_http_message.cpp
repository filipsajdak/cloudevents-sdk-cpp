/// \file
/// \brief Fuzz HTTP message decoding.
///
/// Splits the input into headers and a body, so the fuzzer explores header names,
/// percent-escapes and payloads rather than only one of them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>

namespace {

/// Consume `name: value` lines until a blank line; the rest is the body.
auto build(std::string_view input) -> ce::v2::message {
  ce::v2::message request;
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
      request.header_fields.add(std::string{line.substr(0, colon)},
                                std::string{line.substr(colon + 1)});
    }
    if (line_end == std::string_view::npos) {
      pos = input.size();
      break;
    }
    pos = line_end + 1;
  }
  for (const char character : input.substr(pos)) {
    request.body.push_back(static_cast<std::byte>(character));
  }
  return request;
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view input{reinterpret_cast<const char*>(data), size};
  const ce::v2::message request = build(input);

  (void)ce::v2::http::detect_content_mode(request);
  auto decoded = ce::v2::http::from_message<ce::v2::codec::nlohmann_codec>(request);
  if (decoded) {
    // Anything that decoded must survive a trip back out to a message.
    if (!ce::v2::http::to_message<ce::v2::codec::nlohmann_codec>(*decoded,
                                                         ce::v2::content_mode::binary_mode)) {
      __builtin_trap();
    }
  }
  (void)ce::v2::http::from_batch_message<ce::v2::codec::nlohmann_codec>(request);
  return 0;
}
