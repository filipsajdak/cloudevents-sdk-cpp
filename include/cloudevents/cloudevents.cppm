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
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

export module cloudevents;

// spec: SWR-BUILD-0010
export namespace ce {

using ce::v4::errc;
using ce::v4::error;
using ce::v4::fail;
using ce::v4::result;
using ce::v4::static_error;
using ce::v4::to_string_view;
using ce::v4::widen;

using ce::v4::datacontenttype;
using ce::v4::dataschema;
using ce::v4::extension_name;
using ce::v4::id;
using ce::v4::source;
using ce::v4::spec_version;
using ce::v4::subject;
using ce::v4::type;

namespace literals {
using ce::v4::literals::operator""_dataschema;
using ce::v4::literals::operator""_ext;
using ce::v4::literals::operator""_id;
using ce::v4::literals::operator""_mediatype;
using ce::v4::literals::operator""_source;
using ce::v4::literals::operator""_subject;
using ce::v4::literals::operator""_type;
}  // namespace literals

using ce::v4::attribute_value;
using ce::v4::binary;
using ce::v4::content_mode;
using ce::v4::data_t;
using ce::v4::event;
using ce::v4::raw_headers;
using ce::v4::is_json_content_type;
using ce::v4::json_document;
using ce::v4::json_text;
using ce::v4::lint_warning;
using ce::v4::message;
using ce::v4::parse_timestamp;
using ce::v4::reserved_name;
using ce::v4::timestamp;
using ce::v4::to_bytes;
using ce::v4::to_text;
using ce::v4::to_string;
using ce::v4::uri;
using ce::v4::uri_ref;
using ce::v4::valid_attribute_name;

using ce::v4::backend_of;
using ce::v4::describe_backend;
using ce::v4::described;
using ce::v4::field_count;
using ce::v4::field_names;
using ce::v4::for_each_field;
using ce::v4::members_supported;
using ce::v4::name;
using ce::v4::reflect;
using ce::v4::skip;

using ce::v4::base64_decode;
using ce::v4::base64_encode;
using ce::v4::data_as;
using ce::v4::decode_as;
using ce::v4::decode_batch_as;
using ce::v4::decoded;
using ce::v4::encode_as;
using ce::v4::event_of;
using ce::v4::from_value_as;
using ce::v4::from_json_value;
using ce::v4::json_format;
using ce::v4::set_data;
using ce::v4::to_json_value;

namespace json {
using ce::v4::json::batch_content_type;
using ce::v4::json::content_type;
using ce::v4::json::decode_options;
using ce::v4::json::json_codec;
using ce::v4::json::kind;
using ce::v4::json::string_adopting_codec;
}  // namespace json

namespace binding {
using ce::v4::binding::binding_traits;
using ce::v4::binding::decode_structured;
using ce::v4::binding::encode_structured;
using ce::v4::binding::read_attributes;
using ce::v4::binding::read_body;
using ce::v4::binding::render_attribute;
using ce::v4::binding::write_attributes;
using ce::v4::binding::write_body;
}  // namespace binding

namespace http {
using ce::v4::http::detect_content_mode;
using ce::v4::http::from_batch_message;
using ce::v4::http::from_message;
using ce::v4::http::to_batch_message;
using ce::v4::http::to_message;
}  // namespace http

namespace kafka {
using ce::v4::kafka::detect_content_mode;
using ce::v4::kafka::from_message;
using ce::v4::kafka::key_mapper;
using ce::v4::kafka::no_key_mapper;
using ce::v4::kafka::partitionkey_mapper;
using ce::v4::kafka::record;
using ce::v4::kafka::to_message;
using ce::v4::kafka::to_record;
}  // namespace kafka

namespace nats {
using ce::v4::nats::detect_content_mode;
using ce::v4::nats::from_message;
using ce::v4::nats::from_payload;
using ce::v4::nats::to_message;
using ce::v4::nats::to_payload;
}  // namespace nats

namespace ext {
using ce::v4::ext::ce_describe_fields;
using ce::v4::ext::dataref;
using ce::v4::ext::partitioning;
using ce::v4::ext::sampled_rate;
using ce::v4::ext::sequence;
using ce::v4::ext::tracing;
}  // namespace ext

}  // namespace ce
