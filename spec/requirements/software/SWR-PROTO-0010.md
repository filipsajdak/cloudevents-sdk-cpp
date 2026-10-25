---
uid: SWR-PROTO-0010
title: Decode limits are named constants
type: software
status: reviewed
priority: medium
rationale: >
  The decoder parses untrusted bytes. The schema has a fixed depth (event, map entry, value,
  Timestamp; Any is opaque), so nesting cannot be unbounded, and the limits that remain are
  sizes and counts. The owner ruled the values on 2026-10-25 (CR-0009, item 5): default_max_encoded_size 16 MiB,
  default_max_attribute_count 1024, max_varint_bytes 10 and fixed_nesting_depth 4, and on 2026-10-25 (item 20) that both limits apply to each event
  and that a batch has its own default_max_batch_event_count, proposed at 100,000 events and adjustable in review; none was measured against a
  workload. This requirement states them by name.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The `protobuf::decode_options` shall carry a maximum encoded size and a maximum attribute count,
each defaulting to a named constant of the `protobuf` namespace, and the decoder shall fail with
`errc::out_of_range` before it allocates for an event or a map that exceeds either, each limit applying to each event of a batch, and for a batch of more than
`default_max_batch_event_count` events, and shall use a
fixed nesting depth that is also a named constant.
