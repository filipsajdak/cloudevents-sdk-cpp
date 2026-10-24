---
uid: SWR-MQTT-0010
title: Binary mode reads each User Property named like an attribute as an attribute or an extension
type: software
status: reviewed
priority: high
rationale: >
  Slice 2 (CR-0008). Binding text :156-158: all other context attributes, including extensions, must
  be individually mapped to and from User Properties. An unprefixed name that is a valid attribute
  name is therefore an extension, which is also the cost of the rule: a foreign property with a valid
  name is read as one. The example at 3.1.4 lists datacontenttype as a User Property as well as the
  Content Type; the sketch of the spike accepts the duplicate when it equals the Content Type and
  refuses a conflict.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When the MQTT binding reads a binary-mode message it shall read each User Property whose name is a valid attribute name as that attribute or as an extension, accept a datacontenttype User Property equal to the Content Type, and report errc::invalid_argument when the two differ or when two User Properties carry one attribute name.
