---
uid: SWR-MQTT-0015
title: Binary MQTT messages interoperate with the Go SDK under the prefixed-names policy, and the difference is pinned
type: software
status: reviewed
priority: high
rationale: >
  Slice 2 (CR-0008). The only place the SDKs and the text disagree is the binary-mode prefix
  (docs/spikes/mqtt.md, verdict). With the opt-in the recorded binary corpus decodes and is
  reproduced; with the default every Go binary golden is refused as not_a_cloudevent and our binary
  output decodes in no Go reader, which is the divergence as a test. The difference is recorded as a
  decision in docs/DECISIONS.md and the recorded messages are not edited to hide it, as SWR-SEC-0008
  requires for Kafka.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall decode the binary-mode messages recorded from the Go CloudEvents SDK under the prefixed-names policy to the event that SDK encoded, produce binary-mode messages under that policy that the Go SDK decodes to the event this SDK encoded, and report errc::not_a_cloudevent for each of those recorded messages without the policy.
