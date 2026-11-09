---
uid: SWR-BIND-0020
title: Binary mode refuses a datacontenttype containing CR or LF
type: software
status: reviewed
priority: high
rationale: >
  Binary mode writes datacontenttype into the content-type field without the value policy that
  keeps a line break out of the ce- fields, so a CR or LF in it reaches the HTTP Content-Type
  header, the Kafka content-type header and the MQTT Content Type property as it is. SWR-CORE-0041
  makes such a value invalid, so no event holds one; the owner ruled on 2026-11-10 that the HTTP,
  Kafka and MQTT binary writers check the value they write as well, as defence in depth for a value
  that did not go through the validated attribute (CR-0012). The check sits beside the SWR-BIND-0019
  refusal and refuses the same way, with invalid_argument naming datacontenttype and no message.
  NATS binary mode carries datacontenttype as ce-datacontenttype through its percent encoding and
  is outside this requirement.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When the HTTP, Kafka or MQTT binding of `ce::v4` writes an event in binary mode, it shall check the `datacontenttype` it would write into the content-type field and, if the value contains CR (U+000D) or LF (U+000A), return `errc::invalid_argument` naming `datacontenttype` and produce no message.
