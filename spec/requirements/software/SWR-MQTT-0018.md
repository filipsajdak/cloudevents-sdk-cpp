---
uid: SWR-MQTT-0018
title: The optional module exports the MQTT binding from ce::v4 only
type: software
status: approved
priority: low
rationale: >
  Slice 1 (CR-0008). The optional cloudevents.cppm exports ce::v4 only (SWR-BUILD-0013); the MQTT
  binding is new in v4 and has no earlier generation, so the export is the new namespace alone and
  nothing from v1 to v3 changes. The frozen-API diff (SWR-BUILD-0015) passes it as an addition.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: [code:include/cloudevents/cloudevents.cppm]
verified_by: [test:test/module_test.cpp::module-exports-mqtt-v4-only]
owner: filip.sajdak
version: 1
---
The optional cloudevents.cppm module shall export the MQTT binding of ce::v4 and shall declare no MQTT entity in ce::v1, ce::v2 or ce::v3.
