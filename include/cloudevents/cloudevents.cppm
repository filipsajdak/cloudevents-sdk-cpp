module;

// Every public header is included in the global module fragment, then the names
// are exported. The headers stay the source of truth: a module that redeclared
// them would be a second surface to keep in step (SWR-BUILD-0010).
//
// Kept out of the default build. Module support across the toolchains SPEC
// section 8 names is uneven, and a broken module target would break the floor
// configuration for everyone who never asked for one.

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

export module cloudevents;

// The names live in ce::v1, an inline namespace of ce. A using-declaration must
// name ce::v1 explicitly: naming ce would redeclare each name into the scope it
// already occupies, and nothing would be exported.
export namespace ce {

// --- result and errors ------------------------------------------------------
using ce::v1::errc;
using ce::v1::error;
using ce::v1::fail;
using ce::v1::result;
using ce::v1::to_string_view;

// --- core -------------------------------------------------------------------
using ce::v1::attribute_value;
using ce::v1::binary;
using ce::v1::content_mode;
using ce::v1::data_t;
using ce::v1::event;
using ce::v1::raw_headers;
using ce::v1::is_json_content_type;
using ce::v1::json_text;
using ce::v1::lint_warning;
using ce::v1::message;
using ce::v1::parse_timestamp;
using ce::v1::reserved_name;
using ce::v1::timestamp;
using ce::v1::to_bytes;
using ce::v1::to_text;
using ce::v1::to_string;
using ce::v1::uri;
using ce::v1::uri_ref;
using ce::v1::valid_attribute_name;

// --- describe seam ----------------------------------------------------------
using ce::v1::backend_of;
using ce::v1::describe_backend;
using ce::v1::described;
using ce::v1::field_count;
using ce::v1::field_names;
using ce::v1::for_each_field;
using ce::v1::members_supported;
using ce::v1::name;
using ce::v1::reflect;
using ce::v1::skip;

// --- formats ----------------------------------------------------------------
using ce::v1::base64_decode;
using ce::v1::base64_encode;
using ce::v1::data_as;
using ce::v1::event_of;
using ce::v1::from_json_value;
using ce::v1::json_format;
using ce::v1::set_data;
using ce::v1::to_json_value;

namespace json {
using ce::v1::json::batch_content_type;
using ce::v1::json::content_type;
using ce::v1::json::json_codec;
using ce::v1::json::kind;
}  // namespace json

// --- the shared binding core ------------------------------------------------
namespace binding {
using ce::v1::binding::binding_traits;
using ce::v1::binding::decode_structured;
using ce::v1::binding::encode_structured;
using ce::v1::binding::read_attributes;
using ce::v1::binding::read_body;
using ce::v1::binding::render_attribute;
using ce::v1::binding::write_attributes;
using ce::v1::binding::write_body;
}  // namespace binding

// --- HTTP binding -----------------------------------------------------------
namespace http {
using ce::v1::http::detect_content_mode;
using ce::v1::http::from_batch_message;
using ce::v1::http::from_message;
using ce::v1::http::to_batch_message;
using ce::v1::http::to_message;
}  // namespace http

// --- Kafka binding ----------------------------------------------------------
namespace kafka {
using ce::v1::kafka::detect_content_mode;
using ce::v1::kafka::from_message;
using ce::v1::kafka::key_mapper;
using ce::v1::kafka::no_key_mapper;
using ce::v1::kafka::partitionkey_mapper;
using ce::v1::kafka::record;
using ce::v1::kafka::to_message;
using ce::v1::kafka::to_record;
}  // namespace kafka

// --- NATS binding -----------------------------------------------------------
namespace nats {
using ce::v1::nats::detect_content_mode;
using ce::v1::nats::from_message;
using ce::v1::nats::from_payload;
using ce::v1::nats::to_message;
using ce::v1::nats::to_payload;
}  // namespace nats

// --- documented extensions --------------------------------------------------
namespace ext {
// CE_DESCRIBE defines an ADL hook, and a function declared in the global module
// fragment is not reachable from an importing translation unit unless it is
// exported. Without this, described<ce::ext::tracing> is false on the far side
// of the module boundary and get<>/set<> do not resolve.
using ce::v1::ext::ce_describe_fields;
using ce::v1::ext::dataref;
using ce::v1::ext::partitioning;
using ce::v1::ext::sampled_rate;
using ce::v1::ext::sequence;
using ce::v1::ext::tracing;
}  // namespace ext

}  // namespace ce
