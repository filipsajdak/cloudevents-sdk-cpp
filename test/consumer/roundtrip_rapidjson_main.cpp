#include "roundtrip.hpp"

#include <cloudevents/codec/rapidjson.hpp>

int main() { return consumer::run<ce::codec::rapidjson_codec>(); }
