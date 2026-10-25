---
uid: SWR-PROTO-0024
title: Batch is encoded and decoded as format functions
type: software
status: approved
priority: medium
rationale: >
  Ruled by the owner on 2026-10-25 (CR-0009, item 10). The Protobuf format has a CloudEventBatch message
  (repeated events = 1) and no SDK has a type for it, so the batch is offered as format functions only,
  with the protoc bytes as evidence and no binding batch mode. The owner ruled on 2026-10-25 (item 20) that the size and attribute-count limits apply to each event and that
  the batch has its own named event-count limit, default_max_batch_event_count, proposed at 100,000 events and adjustable in review.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_batch_test.cpp::protobuf-batch-round-trip, test:test/protobuf_batch_test.cpp::protobuf-batch-media-type, test:test/protobuf_batch_test.cpp::protobuf-batch-count-limit, test:test/interop_protobuf_test.cpp::interop-protobuf-batch-golden]
owner: filip.sajdak
version: 1
---
The `protobuf_format` class shall provide `encode_batch`, taking a `std::span<const event>`, and
`decode_batch`, returning a `std::vector<event>`, for a `CloudEventBatch` message, shall expose
application/cloudevents-batch+protobuf as its media type, and shall apply the limits of SWR-PROTO-0010
to each event and fail with `errc::out_of_range` for a batch of more than `default_max_batch_event_count` events, a named constant of the `protobuf` namespace
(proposed value 100,000, adjustable).
