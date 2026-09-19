// Proves the installed ce::codec_nlohmann target still carries its nlohmann link.
//
// install(EXPORT) cannot export a dependency this project does not own, so the
// link is re-attached by cloudeventsConfig.cmake. If that re-attachment breaks,
// nlohmann's headers are not on the include path and this fails to compile -
// which no in-tree build would ever notice.

#include <nlohmann/json.hpp>

#include <cloudevents/detail/config.hpp>

int main() {
  const auto parsed = nlohmann::json::parse(R"({"specversion":"1.0"})", nullptr, false);
  return parsed.is_discarded() ? 1 : 0;
}
