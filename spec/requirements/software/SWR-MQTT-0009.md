---
uid: SWR-MQTT-0009
title: Binary mode on MQTT 5.0 writes attribute names unchanged as User Properties
type: software
status: approved
priority: high
rationale: >
  Slice 2 (CR-0008). Binding text :168-169: attribute names must be used unchanged in each mapped User
  Property; the example at :196-201 shows specversion, type and id with no ce- anywhere in the file.
  Data bytes are the payload as is (:151-152) and datacontenttype goes in the Content Type property
  (:62-66, :146-147). Owner ruling 2026-10-24 (ruling 2): follow the text by default. The same call
  with the opt-in of SWR-MQTT-0012 writes the sdk-go form.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/binding/mqtt.hpp]
verified_by: [test:test/mqtt_binding_test.cpp::mqtt-binary-mode-unprefixed-names, test:test/mqtt_binding_test.cpp::mqtt-binary-mode-no-ce-prefix]
owner: filip.sajdak
version: 1
---
The MQTT binding shall write an event in binary mode as a message whose body is the event data as is, whose Content Type is datacontenttype, and whose User Properties are every other attribute and extension under its own name with no prefix.
