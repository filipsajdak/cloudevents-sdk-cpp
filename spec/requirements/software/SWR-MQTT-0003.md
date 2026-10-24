---
uid: SWR-MQTT-0003
title: The Content Type property is the header content-type and maps to datacontenttype
type: software
status: reviewed
priority: high
rationale: >
  Slice 1 (CR-0008). Binding text :146-147: the Content Type property must be mapped directly to
  datacontenttype in binary mode; in structured mode it holds the event format media type. sdk-go
  reads and writes only Properties.ContentType (message.go:95-97, write_message.go:92). The model
  carries the property as the reserved header content-type, as SWR-KAFKA-0002 does for Kafka, so
  message needs no new member.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The MQTT binding shall carry the Content Type property in a header named content-type, shall map it to and from datacontenttype in binary mode, and shall not write datacontenttype as a User Property.
