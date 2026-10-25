---
uid: SWR-MQTT-0016
title: A libFuzzer target covers the decode path of the MQTT binding
type: software
status: approved
priority: high
rationale: >
  Slices 1 and 2 (CR-0008). SWR-SEC-0001 already requires a target for the decode path of every
  protocol binding, seeded from the conformance fixtures, replayed on every pull request and run for
  ten minutes nightly; a binding parses untrusted User Properties and payloads, and the Kafka and NATS
  targets (fuzz_kafka_message.cpp is 79 lines) are the pattern. This requirement names the MQTT target
  so that the matrix, the corpus and the nightly job gain it with slice 1 and its binary branch with
  slice 2.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:fuzz/fuzz_mqtt_message.cpp, code:fuzz/CMakeLists.txt]
verified_by: [test:fuzz/fuzz_mqtt_message.cpp::fuzz-mqtt-message]
owner: filip.sajdak
version: 1
---
The SDK shall provide a libFuzzer target that feeds arbitrary bytes as a payload, a Content Type and a list of User Properties to the MQTT binding decode entry points, seeded from the recorded messages and the conformance fixtures and replaying its seed corpus on every pull request.
