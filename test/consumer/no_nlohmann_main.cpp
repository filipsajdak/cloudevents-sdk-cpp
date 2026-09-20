/// \file
/// \brief The core, alone, pulls in no third-party library except CTRE.
///
/// Linked against ce::core only. If any header it includes starts to pull
/// nlohmann in, NLOHMANN_JSON_VERSION_MAJOR becomes defined and this stops
/// compiling (SWR-ADOPT-0002).

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <cstdio>

#ifdef NLOHMANN_JSON_VERSION_MAJOR
#error "ce::core pulled in nlohmann; SWR-ADOPT-0002 says it depends on CTRE alone"
#endif

int main() {
  // CTRE is the one permitted dependency, and this exercises it: the attribute
  // name rule is a compile-time regular expression.
  if (!ce::valid_attribute_name("seq9") || ce::valid_attribute_name("Seq9")) {
    return 1;
  }
  ce::event subject{.id = "1", .source = ce::uri_ref{"/core"}, .type = "t"};
  if (!subject.set(ce::ext::partitioning{.partitionkey = "k"})) {
    return 2;
  }
  if (!subject.get<ce::ext::partitioning>()) {
    return 3;
  }
  std::printf("ce::core alone: no nlohmann, CTRE present\n");
  return 0;
}
