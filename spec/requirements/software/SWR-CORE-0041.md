---
uid: SWR-CORE-0041
title: A datacontenttype containing CR or LF is refused as an invalid media type
type: software
status: approved
priority: high
rationale: >
  The CloudEvents core specification makes datacontenttype an RFC 2046 media type: a type, a
  subtype and parameters, none of which may contain CR or LF. RFC 9110 section 5.5 forbids both in a
  field value too, and binary mode writes datacontenttype into the content-type field as it is.
  The media-type grammar the SDK checks (content_type_pattern) admits CR and LF before the type,
  after the subtype and anywhere in the parameter part, so "text/plain; a=b\r\nX-Injected: 1" was a
  valid value that reached an HTTP Content-Type header with a line break in it, in every release
  from v0.1.0 to v0.7.0. The owner ruled on 2026-11-10 that such a value is invalid in every
  generation, refused with the code a malformed media type already returns, and that space and tab
  around the media type stay accepted because a field parser drops them (CR-0012). The check is
  added to the body of the shared ce::v2 policy and of ce::v1's validate(), so no declaration
  changes and the frozen-API gate holds.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp, code:include/cloudevents/v1/core.hpp]
verified_by: [test:test/attribute_types_test.cpp::datacontenttype-refuses-cr-and-lf, test:test/json_format_test.cpp::datacontenttype-cr-lf-in-a-json-document, test:test/protobuf_decode_test.cpp::protobuf-datacontenttype-cr-lf, test:test/http_binding_test.cpp::http-binary-mode-refuses-a-line-break-in-the-media-type, test:test/kafka_binding_test.cpp::kafka-binary-mode-refuses-a-line-break-in-the-media-type, test:test/mqtt_binding_test.cpp::mqtt-binary-mode-refuses-a-line-break-in-the-media-type, test:test/nats_binding_test.cpp::nats-datacontenttype-line-break, test:test/v1/core_test.cpp::v1-validate-refuses-cr-and-lf-in-datacontenttype]
owner: filip.sajdak
version: 1
---
If the text offered for a `datacontenttype` contains CR (U+000D) or LF (U+000A) at any position, then the check behind its factory and its literal in `ce::v2` to `ce::v4`, and `ce::v1::event::validate()`, shall refuse it with `errc::invalid_content_type` naming `datacontenttype`, while still accepting space and tab before or after the media type.
