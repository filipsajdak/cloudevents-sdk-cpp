/// \file
/// \brief What an installed consumer reaches with ce::binding_mqtt and nothing else.
///
/// The link line is ce::binding_mqtt alone: the binding depends on ce::core and on no MQTT client
/// library and no JSON codec (SWR-MQTT-0017).

#include <cstdio>

#include <cloudevents/binding/mqtt.hpp>
#include <cloudevents/message.hpp>

int main() {
  const ce::message structured{.header_fields = {{"content-type", "application/cloudevents+json"}}};
  if (ce::mqtt::detect_content_mode(structured) != ce::content_mode::structured) {
    return 1;
  }
  const ce::message plain{};
  if (ce::mqtt::detect_content_mode(plain) != ce::content_mode::binary_mode) {
    return 2;
  }
  std::printf("the MQTT binding is reachable from the installed package\n");
  return 0;
}
