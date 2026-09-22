/// \file
/// \brief What an installed consumer can reach, and in which direction.
///
/// The value is in the link line, not in this file: it is built against the
/// IMPORTED targets from find_package, so a dependency the package fails to
/// carry shows up as a link or include failure here (SWR-ADOPT-0001).

#include <cloudevents/binding/http.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>

#include <cstdio>

// The direction is downward: core knows nothing of the format layer, and the
// format layer knows nothing of the binding. A header that reached back upward
// would make this translation unit fail to compile with only ce::core linked,
// which consumer_no_nlohmann below is built to show.

int main() {
  using namespace ce::literals;
  const ce::event subject{"1"_id, "/graph"_source, "t"_type};
  if (subject.id().view() != "1") {
    return 1;
  }
  const ce::message request{.header_fields = {{"ce-id", "1"}}};
  if (!request.header_fields.contains("CE-ID")) {
    return 2;
  }
  std::printf("core, format and binding all reachable from the installed package\n");
  return 0;
}
