---
uid: SWR-PROTO-0010
title: Decode limits are named constants
type: software
status: reviewed
priority: medium
rationale: >
  The decoder parses untrusted bytes. The schema has a fixed depth (event, map entry, value,
  Timestamp; Any is opaque), so nesting cannot be unbounded, and the limits that remain are
  sizes and counts. The values are proposals for the owner to confirm (CR-0009, open question
  1); this requirement states them by name.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf::decode_options` shall carry a maximum encoded size and a maximum attribute count,
each defaulting to a named constant of the `protobuf` namespace, and the decoder shall fail with
`errc::out_of_range` before it allocates for input or a map that exceeds either, and shall use a
fixed nesting depth that is also a named constant.
