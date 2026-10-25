/// \file
/// \brief Emits the Protobuf bytes the Go and Java SDKs are asked to decode.
///
/// One `<name>.pb` per corpus event, the same events interop/protobuf/go and
/// interop/protobuf/java write, from this side (SWR-PROTO-0016). Built by
/// interop/run.sh, not by the default CMake build.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <cloudevents/format/protobuf_format.hpp>

#include "corpus.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: produce <output-directory>\n");
    return 2;
  }
  const std::filesystem::path dir{argv[1]};
  std::filesystem::create_directories(dir);

  int failures = 0;
  for (const auto& [name, subject] : ce::interop::protobuf_corpus::events()) {
    const auto encoded = ce::protobuf_format::encode(subject);
    if (!encoded) {
      std::fprintf(
          stderr, "FAIL %s: %s\n", std::string{name}.c_str(), encoded.error().detail.c_str());
      ++failures;
      continue;
    }
    const std::string file = std::string{name} + ".pb";
    std::ofstream out{dir / file, std::ios::binary};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(encoded->data()),
              static_cast<std::streamsize>(encoded->size()));
    std::printf("wrote %s\n", file.c_str());
  }
  return failures == 0 ? 0 : 1;
}
