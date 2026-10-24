---
uid: SWR-MQTT-0011
title: User Property values are canonical strings, valid UTF-8 without NUL
type: software
status: reviewed
priority: medium
rationale: >
  Slice 2 (CR-0008). Binding text :173-175: values are the attribute type's canonical string
  representation. An MQTT UTF-8 Encoded String must be well-formed UTF-8 and must not contain U+0000
  (OASIS MQTT 5.0, 1.5.4). UNVERIFIED: the 65,535-byte limit of a UTF-8 Encoded String was not re-read
  in the spike; the stage reads OASIS 1.5.4 before writing the test, and the statement then names the
  limit. There is no percent-encoding, as in the Kafka binding.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The MQTT binding shall write each attribute value in its canonical string form, report errc::invalid_utf8 for a value that is not well-formed UTF-8, and report errc::invalid_argument for a value that contains NUL.
