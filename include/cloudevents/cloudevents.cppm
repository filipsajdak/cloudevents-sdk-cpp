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

using ce::v2::errc;
using ce::v2::error;
using ce::v2::fail;
using ce::v2::result;
using ce::v2::static_error;
using ce::v2::to_string_view;
using ce::v2::widen;

using ce::v2::datacontenttype;
using ce::v2::dataschema;
using ce::v2::extension_name;
using ce::v2::id;
using ce::v2::source;
using ce::v2::spec_version;
using ce::v2::subject;
using ce::v2::type;

namespace literals {
using ce::v2::literals::operator""_dataschema;
using ce::v2::literals::operator""_ext;
using ce::v2::literals::operator""_id;
using ce::v2::literals::operator""_mediatype;
using ce::v2::literals::operator""_source;
using ce::v2::literals::operator""_subject;
using ce::v2::literals::operator""_type;
}  // namespace literals

using ce::v2::attribute_value;
using ce::v2::binary;
using ce::v2::content_mode;
using ce::v2::data_t;
using ce::v2::event;
using ce::v2::raw_headers;
using ce::v2::is_json_content_type;
using ce::v2::json_text;
using ce::v2::lint_warning;
using ce::v2::message;
using ce::v2::parse_timestamp;
using ce::v2::reserved_name;
using ce::v2::timestamp;
using ce::v2::to_bytes;
using ce::v2::to_text;
using ce::v2::to_string;
using ce::v2::uri;
using ce::v2::uri_ref;
using ce::v2::valid_attribute_name;

using ce::v2::backend_of;
using ce::v2::describe_backend;
using ce::v2::described;
using ce::v2::field_count;
using ce::v2::field_names;
using ce::v2::for_each_field;
using ce::v2::members_supported;
using ce::v2::name;
using ce::v2::reflect;
using ce::v2::skip;

using ce::v2::base64_decode;
using ce::v2::base64_encode;
using ce::v2::data_as;
using ce::v2::event_of;
using ce::v2::from_json_value;
using ce::v2::json_format;
using ce::v2::set_data;
using ce::v2::to_json_value;

namespace json {
using ce::v2::json::batch_content_type;
using ce::v2::json::content_type;
using ce::v2::json::json_codec;
using ce::v2::json::kind;
}  // namespace json

namespace binding {
using ce::v2::binding::binding_traits;
using ce::v2::binding::decode_structured;
using ce::v2::binding::encode_structured;
using ce::v2::binding::read_attributes;
using ce::v2::binding::read_body;
using ce::v2::binding::render_attribute;
using ce::v2::binding::write_attributes;
using ce::v2::binding::write_body;
}  // namespace binding

namespace http {
using ce::v2::http::detect_content_mode;
using ce::v2::http::from_batch_message;
using ce::v2::http::from_message;
using ce::v2::http::to_batch_message;
using ce::v2::http::to_message;
}  // namespace http

namespace kafka {
using ce::v2::kafka::detect_content_mode;
using ce::v2::kafka::from_message;
using ce::v2::kafka::key_mapper;
using ce::v2::kafka::no_key_mapper;
using ce::v2::kafka::partitionkey_mapper;
using ce::v2::kafka::record;
using ce::v2::kafka::to_message;
using ce::v2::kafka::to_record;
}  // namespace kafka

namespace nats {
using ce::v2::nats::detect_content_mode;
using ce::v2::nats::from_message;
using ce::v2::nats::from_payload;
using ce::v2::nats::to_message;
using ce::v2::nats::to_payload;
}  // namespace nats

namespace ext {
using ce::v2::ext::ce_describe_fields;
using ce::v2::ext::dataref;
using ce::v2::ext::partitioning;
using ce::v2::ext::sampled_rate;
using ce::v2::ext::sequence;
using ce::v2::ext::tracing;
}  // namespace ext

}  // namespace ce
