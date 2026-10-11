---
uid: SWR-BIND-0009
title: Every binding entry point that decodes JSON takes the JSON decode options
type: software
status: implemented
delivered_in: v0.6.0
priority: medium
rationale: >
  In ce::v3 a binding's from_message decoded a structured message with the default decode options and gave the caller no way to set the retention limit, and http::from_batch_message and nats::from_payload did the same for a batch and a NATS payload.
  In ce::v4 the binary path parses JSON bodies as well (SWR-BIND-0006), so every path that reads JSON retains documents and every one needs the limit.
  The owner decided on 2026-09-28 that from_message takes json::decode_options with the same 16 KiB default, and on 2026-09-29 that every v4 binding entry point that decodes JSON takes it, http::from_batch_message and nats::from_payload included.
  json_format::from_value and from_value_as are not binding entry points and parse no text: they receive a DOM the caller already holds, so the retention limit has nothing to bound, and the owner confirmed on 2026-09-29 that they take no decode options (D-JSON-6).
  One parameter per entry point, applied to every content mode it reads, means a caller that lowers the limit for untrusted input cannot miss one mode or one entry point.
  A defaulted last parameter, as json_format::decode takes it (D-JSON-5), keeps every existing call spelled the same.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp, code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/kafka.hpp, code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/http_binding_test.cpp::from-message-takes-decode-options, test:test/kafka_binding_test.cpp::from-message-takes-decode-options, test:test/nats_binding_test.cpp::from-message-takes-decode-options, test:test/http_binding_test.cpp::from-batch-message-takes-decode-options, test:test/nats_binding_test.cpp::from-payload-takes-decode-options]
owner: filip.sajdak
version: 3
---
The HTTP, Kafka and NATS bindings shall give `from_message<Codec>` of each binding, `http::from_batch_message<Codec>` and `nats::from_payload<Codec>` a `json::decode_options` argument, defaulting to a retention limit of 16 KiB, and apply it to every JSON payload the entry point decodes, in structured, batched and binary mode.
This does not extend to `json_format::from_value` or `from_value_as`, which take a document the caller already parsed and no decode options.
