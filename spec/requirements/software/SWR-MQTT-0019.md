---
uid: SWR-MQTT-0019
title: The user guide documents the MQTT binding and its prefix opt-in
type: software
status: reviewed
priority: low
rationale: >
  Slices 1 and 2 (CR-0008). docs/GUIDE.md has a section per binding whose code the example_guide test
  builds and runs (D-DOCS-1). The MQTT section states the version rule (5.0 against 3.1.1), the name
  rule and the opt-in, the Content Type property and the open treatment of foreign properties, so that
  a reader who talks to a Go publisher finds the opt-in without reading the decisions.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The user guide shall describe the MQTT binding entry points, the 3.1.1 payload entry points and the prefixed-names policy with code that the guide test builds and runs.
