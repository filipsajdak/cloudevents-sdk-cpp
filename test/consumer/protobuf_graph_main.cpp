/// \file
/// \brief What ce::format_protobuf reaches on its own, and in which direction (SWR-PROTO-0014).
///
/// It is linked to ce::format_protobuf alone. CTRE comes through ce::core, which the target
/// links, and no other target is on the link line: a header of the format that included a codec
/// or a binding would fail to compile here.

#include <cstdio>
#include <string_view>

#include <cloudevents/format/protobuf_format.hpp>

int main() {
  constexpr std::string_view media_type = ce::protobuf_format::content_type;
  if (media_type != "application/cloudevents+protobuf") {
    return 1;
  }
  std::printf("the Protobuf format is reachable from ce::format_protobuf alone\n");
  return 0;
}
