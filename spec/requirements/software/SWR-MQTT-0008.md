---
uid: SWR-MQTT-0008
title: A User Property named content-type is not forwarded as the Content Type property
type: software
status: reviewed
priority: medium
rationale: >
  Slice 1 (CR-0008). OPEN QUESTION 2, proposal only: the Content Type property and a User Property
  share one namespace once both are header fields, and MQTT allows a User Property literally named
  content-type. It collides with no attribute (names are [a-z0-9]+), so it is no break, but an adapter
  that copies User Properties cannot tell it from the property. The proposal refuses it where the
  adapter is called; the alternative is to drop it. The owner decides; this statement changes with the
  ruling and is not approved before it.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
If a message handed to the MQTT binding carries a User Property named content-type in addition to the Content Type property, then the binding shall report errc::invalid_argument rather than read the User Property as the Content Type.
