---
uid: SWR-MQTT-0008
title: A User Property named content-type is not forwarded as the Content Type property
type: software
status: approved
priority: medium
rationale: >
  Slice 1 (CR-0008). Ruled by the owner on 2026-10-25 (CR-0008, item 6): the Content Type property and a User Property
  share one namespace once both are header fields, and MQTT allows a User Property literally named
  content-type. It collides with no attribute (names are [a-z0-9]+), so it is no break, but an adapter
  that copies User Properties cannot tell it from the property. The ruling refuses it where the
  adapter is called; the alternative, to drop it, is declined.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-user-property-named-content-type]
owner: filip.sajdak
version: 1
---
If a message handed to the MQTT binding carries a User Property named content-type in addition to the Content Type property, then the binding shall report errc::invalid_argument rather than read the User Property as the Content Type.
