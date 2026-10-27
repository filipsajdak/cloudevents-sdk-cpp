/// \file
/// \brief Emits the MQTT 5.0 PUBLISH messages the Go SDK is asked to accept, in both modes.
///
/// A message is the topic, the payload, the Content Type property and the User Properties this
/// SDK's MQTT binding puts on a PUBLISH, in the JSON shape the Go generator writes, so that no
/// broker is involved (SWR-MQTT-0014). Built by interop/run.sh, not by the default CMake build.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <cloudevents/binding/mqtt.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>

#include "corpus.hpp"

namespace {

using codec = ce::codec::nlohmann_codec;

/// JSON object keys keep the order they are inserted in, as in the Go generator.
using ordered_json = nlohmann::ordered_json;

constexpr int json_indent = 2;
constexpr std::string_view protocol_version = "5.0";
constexpr std::string_view topic = "mytopic";
constexpr std::string_view content_type_name = "content-type";
constexpr std::string_view structured = "structured";
constexpr std::string_view binary = "binary";

int failures = 0;

/// A PUBLISH as the Go generator writes it. The Content Type property is null when the message
/// carries none, and every other header is a User Property, in order.
[[nodiscard]] auto document_of(const ce::message& produced) -> ordered_json {
  auto user_properties = ordered_json::array();
  for (const auto& [name, value] : produced.header_fields) {
    if (name != content_type_name) {
      user_properties.push_back(ordered_json::array({name, value}));
    }
  }
  const std::string* declared = produced.header_fields.find_exact(content_type_name);
  // Parentheses: a braced one-element list would build an array, not a string.
  const ordered_json content_type =
      declared != nullptr ? ordered_json(*declared) : ordered_json(nullptr);
  return ordered_json{
      {"mqtt", protocol_version},
      {"topic", topic},
      {"payload_base64", ce::base64_encode(produced.body)},
      {"content_type", content_type},
      {"user_properties", std::move(user_properties)},
  };
}

/// One message of the corpus in one mode. Binary mode is written under `prefixed_names`, the only
/// form the Go SDK reads (SWR-MQTT-0015); structured mode has no names to choose.
void write(const std::filesystem::path& dir,
           const mqtt_interop::named_event& entry,
           std::string_view mode) {
  auto produced = mode == structured
                      ? ce::mqtt::to_message<codec>(entry.subject, ce::content_mode::structured)
                      : ce::mqtt::to_message<codec, ce::mqtt::prefixed_names>(
                            entry.subject, ce::content_mode::binary_mode);
  const std::string file = entry.name + "." + std::string{mode} + ".json";
  if (!produced) {
    std::fprintf(stderr, "FAIL %s: %s\n", file.c_str(), produced.error().detail.c_str());
    ++failures;
    return;
  }
  std::ofstream out{dir / file, std::ios::binary};
  out << document_of(*produced).dump(json_indent) << '\n';
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

  for (const auto& entry : mqtt_interop::corpus("cpp")) {
    for (const auto mode : {structured, binary}) {
      write(dir, entry, mode);
    }
  }
  return failures == 0 ? 0 : 1;
}
