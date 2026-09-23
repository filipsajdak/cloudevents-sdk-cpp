---
uid: SWR-JSON-0039
title: The v3 codec concept requires value equality and a deep copy
type: software
status: reviewed
priority: high
rationale: >
  The encoder copies a retained DOM into its output document, and document equality needs the codec's own comparison.
  RapidJSON's value cannot be copy-constructed, so the codec supplies the deep copy the encoder needs rather than the concept requiring a copyable value.
  CR-0003 requires both of every v3 codec, so each operation has one code path.
  The v1 and v2 concepts are unchanged, so a codec written for them keeps working there.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The v3 `json_codec` concept shall require a codec to provide `equal(const value&, const value&) -> bool` and `copy(const value&) -> value`.
