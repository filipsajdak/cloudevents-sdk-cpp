---
uid: SWR-BIND-0021
title: Binary mode refuses binary data that a JSON media type would make unreadable
type: software
status: reviewed
priority: high
rationale: >
  Binary mode writes a ce::binary payload as the body whatever the datacontenttype, and the v4
  reader parses every non-empty binary-mode body under a media type that is_json_content_type
  accepts (SWR-BIND-0008), so an event with binary data that is not JSON under such a media type
  wrote a message that its own reader refused with parse_error: the event was lost at the receiver,
  never replaced. The scheduled fuzz run of 2026-11-07 found it in MQTT beside the defect of
  CR-0010, and HTTP (since v0.1.0) and Kafka (since v0.2.0) have the same write and read. The owner
  ruled on 2026-11-09 that the writer refuses the event instead of leaving it a known limitation or
  changing the reader (CR-0011). The writer parses with the codec of to_message, the call the reader
  makes, so "valid JSON" is the writer's codec accepting the bytes. An empty payload is not refused,
  because the reader takes an empty body for no payload (SWR-BIND-0010). The refusal applies
  whatever the reader's parse_binary_body option, which the writer does not know (SWR-BIND-0012).
  An event that SWR-BIND-0019 refuses is refused there first. Structured mode, MQTT 3.1.1 and the
  Protobuf entry points are unaffected, and so are the frozen generations, whose readers do not
  parse the body. Whether NATS takes the same refusal is an open question of CR-0011.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
owner: filip.sajdak
version: 1
---
When the HTTP, Kafka or MQTT binding of `ce::v4` writes an event in binary mode, and the event's `datacontenttype` is a media type that `is_json_content_type` accepts, and its payload is a non-empty `ce::binary` that the codec of `to_message` does not parse as JSON, `to_message` shall return `errc::invalid_argument` and produce no message.
