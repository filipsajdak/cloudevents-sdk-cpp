---
uid: SWR-MQTT-0017
title: The MQTT binding is a header-only target with no new dependency
type: software
status: approved
priority: medium
rationale: >
  Slice 1 (CR-0008). Kafka and NATS are header-only targets over ce::core (SWR-BUILD-0004) and the
  installed package must stay consumable through find_package (SWR-ADOPT-0002). The MQTT binding names
  no client library, so it adds a target, an install entry and an export, and no dependency; the core
  stays CTRE-only (SWR-ADOPT-0001).
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:CMakeLists.txt]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-mqtt-target]
owner: filip.sajdak
version: 1
---
The SDK shall provide the header include/cloudevents/binding/mqtt.hpp as a header-only CMake target that depends on ce::core and on no MQTT client library, and shall install it with the other binding targets.
