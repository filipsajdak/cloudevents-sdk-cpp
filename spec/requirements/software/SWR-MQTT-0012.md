---
uid: SWR-MQTT-0012
title: The ce- prefix of sdk-go is an opt-in name policy, defaulting to the text
type: software
status: reviewed
priority: high
rationale: >
  Slice 2 (CR-0008). Owner ruling 2026-10-24 (ruling 2): follow the text by default and offer an
  opt-in for the prefix sdk-go writes and requires on read and write (message.go:20,
  write_message.go:97). Carrier: a defaulted trailing template parameter naming a policy type, as
  D-HTTP-1 did for http::literal_values and ADR-0014 argues: the prefix is a compile-time constant of
  the traits, and a new function with a defaulted parameter changes no published declaration. The
  policy is named at the call site, so the departure from the text is visible in the code that chose
  it.
verification_method: test
security_classification: operational
derived_from: [SYS-MQTT-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
Where a caller names the prefixed-names policy, the MQTT binding shall write every attribute and extension in binary mode as a User Property named ce- followed by the attribute name and read only User Properties whose name starts with ce-, and without it the binding shall use the names unchanged.
