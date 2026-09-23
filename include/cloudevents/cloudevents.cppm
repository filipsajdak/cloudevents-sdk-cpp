module;

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

export module cloudevents;

// spec: SWR-BUILD-0010
export namespace ce {

using ce::v3::errc;
using ce::v3::error;
using ce::v3::fail;
using ce::v3::result;
using ce::v3::static_error;
using ce::v3::to_string_view;
using ce::v3::widen;

using ce::v3::datacontenttype;
using ce::v3::dataschema;
using ce::v3::extension_name;
using ce::v3::id;
using ce::v3::source;
using ce::v3::spec_version;
using ce::v3::subject;
using ce::v3::type;

namespace literals {
using ce::v3::literals::operator""_dataschema;
using ce::v3::literals::operator""_ext;
using ce::v3::literals::operator""_id;
using ce::v3::literals::operator""_mediatype;
using ce::v3::literals::operator""_source;
using ce::v3::literals::operator""_subject;
using ce::v3::literals::operator""_type;
}  // namespace literals

using ce::v3::attribute_value;
using ce::v3::binary;
using ce::v3::content_mode;
using ce::v3::data_t;
using ce::v3::event;
using ce::v3::raw_headers;
using ce::v3::is_json_content_type;
using ce::v3::json_text;
using ce::v3::lint_warning;
using ce::v3::message;
using ce::v3::parse_timestamp;
using ce::v3::reserved_name;
using ce::v3::timestamp;
using ce::v3::to_bytes;
using ce::v3::to_text;
using ce::v3::to_string;
using ce::v3::uri;
using ce::v3::uri_ref;
using ce::v3::valid_attribute_name;

using ce::v3::backend_of;
using ce::v3::describe_backend;
using ce::v3::described;
using ce::v3::field_count;
using ce::v3::field_names;
using ce::v3::for_each_field;
using ce::v3::members_supported;
using ce::v3::name;
using ce::v3::reflect;
using ce::v3::skip;

using ce::v3::base64_decode;
using ce::v3::base64_encode;
using ce::v3::data_as;
using ce::v3::event_of;
using ce::v3::from_json_value;
using ce::v3::json_format;
using ce::v3::set_data;
using ce::v3::to_json_value;

namespace json {
using ce::v3::json::batch_content_type;
using ce::v3::json::content_type;
using ce::v3::json::json_codec;
using ce::v3::json::kind;
}  // namespace json

namespace binding {
using ce::v3::binding::binding_traits;
using ce::v3::binding::decode_structured;
using ce::v3::binding::encode_structured;
using ce::v3::binding::read_attributes;
using ce::v3::binding::read_body;
using ce::v3::binding::render_attribute;
using ce::v3::binding::write_attributes;
using ce::v3::binding::write_body;
}  // namespace binding

namespace http {
using ce::v3::http::detect_content_mode;
using ce::v3::http::from_batch_message;
using ce::v3::http::from_message;
using ce::v3::http::to_batch_message;
using ce::v3::http::to_message;
}  // namespace http

namespace kafka {
using ce::v3::kafka::detect_content_mode;
using ce::v3::kafka::from_message;
using ce::v3::kafka::key_mapper;
using ce::v3::kafka::no_key_mapper;
using ce::v3::kafka::partitionkey_mapper;
using ce::v3::kafka::record;
using ce::v3::kafka::to_message;
using ce::v3::kafka::to_record;
}  // namespace kafka

namespace nats {
using ce::v3::nats::detect_content_mode;
using ce::v3::nats::from_message;
using ce::v3::nats::from_payload;
using ce::v3::nats::to_message;
using ce::v3::nats::to_payload;
}  // namespace nats

namespace ext {
using ce::v3::ext::ce_describe_fields;
using ce::v3::ext::dataref;
using ce::v3::ext::partitioning;
using ce::v3::ext::sampled_rate;
using ce::v3::ext::sequence;
using ce::v3::ext::tracing;
}  // namespace ext

}  // namespace ce
