/// \file
/// \brief An installed consumer round-trips an event through ce::format_protobuf (SWR-PROTO-0014).
///
/// It links the one target and nothing else: the format needs no codec and no third-party
/// library, so everything it uses must arrive through ce::format_protobuf and the ce::core it
/// links.

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>

int main() {
  using namespace ce::literals;
  using format = ce::protobuf_format;

  const ce::event sent{
      "1"_id,
      "/consumer"_source,
      "com.example.consumer"_type,
      {
          .datacontenttype = "text/plain"_mediatype,
          .subject = "installed"_subject,
          .extensions = {{"count"_ext, std::int32_t{3}}},
          .data = std::string{"payload"},
      },
  };

  const auto encoded = format::encode(sent);
  if (!encoded) {
    return 1;
  }
  const auto received = format::decode(*encoded);
  if (!received || !(*received == sent)) {
    return 2;
  }

  const std::vector<ce::event> batch{sent, sent};
  const auto batch_bytes = format::encode_batch(batch);
  const auto batch_back =
      batch_bytes ? format::decode_batch(*batch_bytes) : ce::result<std::vector<ce::event>>{};
  if (!batch_back || batch_back->size() != batch.size() || !(batch_back->front() == sent)) {
    return 3;
  }

  std::printf("an event round-tripped through the installed Protobuf format\n");
  return 0;
}
