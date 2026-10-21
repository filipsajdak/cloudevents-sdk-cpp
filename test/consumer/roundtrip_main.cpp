#include "roundtrip.hpp"

#include <cloudevents/codec/nlohmann.hpp>

int main() { return consumer::run<ce::codec::nlohmann_codec>(); }
