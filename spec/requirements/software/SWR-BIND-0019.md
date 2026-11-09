---
uid: SWR-BIND-0019
title: Binary mode refuses a datacontenttype that the reader takes for a CloudEvents format
type: software
status: approved
priority: high
rationale: >
  Binary mode writes datacontenttype into the content-type field, and the HTTP, Kafka and MQTT
  readers decide the mode from that field first: a media type starting with application/cloudevents
  is structured mode, or batched mode for application/cloudevents-batch (SWR-HTTP-0006,
  SWR-KAFKA-0005, SWR-MQTT-0004). An event declaring such a datacontenttype therefore wrote a
  binary-mode message that read back as a structured one: its data was parsed as an event, so the
  event was lost, or, when the data was itself a CloudEvent, replaced by that inner event, which lets
  a relay that forwards in binary mode hand its consumer another event. The scheduled fuzz run of
  2026-11-07 found it in MQTT, and HTTP (since v0.1.0) and Kafka (since v0.2.0) have the same write and
  read. The owner ruled on 2026-11-09 that the writer refuses the event, testing the media type as
  the reader does so the two cannot disagree (a test holds them together): ignoring case in HTTP,
  byte for byte in Kafka and MQTT,
  and parameters change nothing because the test is a prefix (CR-0010). Structured mode carries
  datacontenttype inside the document and is unaffected; NATS binary mode carries it as
  ce-datacontenttype and is unaffected. The frozen generations are outside this requirement until the
  owner rules on them (CR-0010). The media-type grammar admits whitespace before the type, and an
  HTTP field parser drops leading spaces and tabs from a field value, so " application/cloudevents+json"
  would reach a reader behind such a parser as a structured message: the test is made on the value
  without its leading whitespace (space, tab, CR, LF, VT, FF), a review finding of 2026-11-09 (CR-0010).
  The readers of this SDK do not trim, so the writer refuses a little more than they would misread.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp, code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/http_binding_test.cpp::binary-mode-refuses-a-cloudevents-media-type, test:test/kafka_binding_test.cpp::kafka-binary-mode-refuses-a-cloudevents-media-type]
owner: filip.sajdak
version: 1
---
When the HTTP, Kafka or MQTT binding of `ce::v4` writes an event in binary mode and the event's `datacontenttype`, after the leading whitespace its media-type grammar admits, starts with `application/cloudevents`, compared as that binding's `detect_content_mode` compares a received content type, `to_message` shall return `errc::invalid_argument` and produce no message.
