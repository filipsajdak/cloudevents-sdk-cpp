/// \file
/// \brief Emits the Protobuf bytes and messages the Go and Java SDKs are asked to decode.
///
/// One `<name>.pb` per corpus event, the same events interop/protobuf/go and
/// interop/protobuf/java write, from this side (SWR-PROTO-0016); and the structured message of
/// the HTTP and the Kafka binding for each, as `<name>.http` and `<name>.kafka`, with the headers
/// one per line, an empty line, then the body (SWR-PROTO-0023). Built by interop/run.sh, not by
/// the default CMake build.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "corpus.hpp"

namespace {

void write_bytes(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out{path, std::ios::binary};
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] auto message_file(const ce::message& message) -> std::string {
  std::string out;
  for (const auto& [name, value] : message.header_fields) {
    out.append(name).append(": ").append(value).append("\n");
  }
  out.append("\n").append(ce::to_text(message.body));
  return out;
}

int failures = 0;

void write_message(const std::filesystem::path& dir,
                   std::string_view name,
                   std::string_view transport,
                   const ce::result<ce::message>& message) {
  if (!message) {
    std::fprintf(stderr,
                 "FAIL %s.%s: %s\n",
                 std::string{name}.c_str(),
                 std::string{transport}.c_str(),
                 message.error().detail.c_str());
    ++failures;
    return;
  }
  const std::string file = std::string{name} + "." + std::string{transport};
  write_bytes(dir / file, message_file(*message));
  std::printf("wrote %s\n", file.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: produce <output-directory>\n");
    return 2;
  }
  const std::filesystem::path dir{argv[1]};
  std::filesystem::create_directories(dir);

  for (const auto& [name, subject] : ce::interop::protobuf_corpus::events()) {
    const auto encoded = ce::protobuf_format::encode(subject);
    if (!encoded) {
      std::fprintf(
          stderr, "FAIL %s: %s\n", std::string{name}.c_str(), encoded.error().detail.c_str());
      ++failures;
      continue;
    }
    const std::string file = std::string{name} + ".pb";
    write_bytes(dir / file, ce::to_text(*encoded));
    std::printf("wrote %s\n", file.c_str());
    write_message(dir, name, "http", ce::http::to_message_protobuf(subject));
    write_message(dir, name, "kafka", ce::kafka::to_message_protobuf(subject));
  }
  return failures == 0 ? 0 : 1;
}
