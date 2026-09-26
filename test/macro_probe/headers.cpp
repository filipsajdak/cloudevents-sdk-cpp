// Preprocessed with -dM -E, never compiled: its output is the macro state a
// consumer is left with after including every public header of every
// generation. macro_leakage_test.cpp reads it (SWR-BUILD-0009).
#include <cloudevents/attributes.hpp>
#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/http.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/binding/nats.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>
#include <cloudevents/v1/binding/common.hpp>
#include <cloudevents/v1/binding/http.hpp>
#include <cloudevents/v1/binding/kafka.hpp>
#include <cloudevents/v1/binding/nats.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/v1/extensions.hpp>
#include <cloudevents/v1/format/describe_json.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/v1/format/typed_payload.hpp>
#include <cloudevents/v1/message.hpp>
#include <cloudevents/v2/binding/common.hpp>
#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/v2/binding/kafka.hpp>
#include <cloudevents/v2/binding/nats.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/v2/format/json_format.hpp>
#include <cloudevents/v2/format/typed_payload.hpp>

// A codec header exists in a build only when its codec is in CE_CODECS. The
// switches are not CE_-prefixed, so they cannot reach the set under test.
#if defined(MACRO_PROBE_CODEC_NLOHMANN)
#include <cloudevents/codec/nlohmann.hpp>
#endif
#if defined(MACRO_PROBE_CODEC_RAPIDJSON)
#include <cloudevents/codec/rapidjson.hpp>
#endif
#if defined(MACRO_PROBE_CODEC_BOOST_JSON)
#include <cloudevents/codec/boost_json.hpp>
#endif
