---
uid: SWR-BUILD-0005
title: Public API published through the inline namespace of the current generation
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 4 places the public API in an inline versioned namespace so the
  mangled names carry the API generation and a consumer linking two generations
  gets a link error instead of silent one-definition-rule breakage. v0.6.0 is the
  fourth generation (CR-0004), so the inline namespace is `ce::v4`; an entity that
  did not change stays declared in `ce::v3`, `ce::v2` or `ce::v1`, whichever
  generation first published it, and is brought into `ce::v4`, so every spelling
  names one type.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/core.hpp, code:include/cloudevents/attributes.hpp, code:include/cloudevents/result.hpp, code:include/cloudevents/describe.hpp, code:include/cloudevents/detail/timestamp.hpp, code:include/cloudevents/detail/validated_string.hpp, code:include/cloudevents/message.hpp, code:include/cloudevents/extensions.hpp, code:include/cloudevents/format/base64.hpp, code:include/cloudevents/format/json_codec.hpp, code:include/cloudevents/format/describe_json.hpp, code:include/cloudevents/binding/detail/percent.hpp, code:include/cloudevents/codec/nlohmann.hpp, code:include/cloudevents/codec/boost_json.hpp, code:include/cloudevents/codec/rapidjson.hpp, code:include/cloudevents/detail/json_document_model.hpp, code:include/cloudevents/format/decode_options.hpp]
verified_by: [test:test/build_test.cpp::config-inline-namespace-v4]
owner: filip.sajdak
version: 4
---
The SDK shall declare every public entity of the current API generation inside the inline
namespace `ce::v4`, or bring it into `ce::v4` with a using-declaration where the entity is
unchanged from `ce::v3`, `ce::v2` or `ce::v1`.
