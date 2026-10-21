---
uid: SWR-BIND-0014
title: A moving entry point returns what the copying entry point returns, taking a non-JSON body without a copy
type: software
status: reviewed
priority: medium
rationale: >
  CR-0005 adds entry points that move a message's body so a receive does not copy it. They are new
  names, not overloads, because a same-name from_message(message&&) would change what
  from_message<C>(std::move(m)) does to m, which compiles today and leaves m intact (rule 4 analysis,
  CR-0005). A move can only reach the ce::binary payload, since message::body is a byte vector and
  json_text holds a std::string (CR-0004 item 4), so the saving is one allocation and one copy for a
  non-JSON body. The owner chose the name from_message_moving and
  v4 only on 2026-10-21, after the measurement of PR #93 (one of four allocations saved, 5.5% to 7.8%
  of the instructions at 256 B), and ruled the same day that a JSON, structured or batch body takes
  the copying path under the same name, so one name serves every body and a caller does not have to
  know which bodies a move could help. A body is moved only when read_body returns it as the
  ce::binary payload: a non-empty body of a binary-mode message whose media type is absent or not one
  is_json_content_type accepts. Every other body is read as the copying entry point reads it.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp, code:include/cloudevents/binding/http.hpp, code:include/cloudevents/binding/kafka.hpp, code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/binding_core_test.cpp::moving-reader-agrees-with-the-copying-reader, test:test/binding_core_test.cpp::moving-reader-takes-a-non-json-body-without-a-copy, test:test/binding_core_test.cpp::moving-reader-takes-the-copying-path-for-a-json-body, test:test/http_binding_test.cpp::from-message-moving-agrees-with-from-message, test:test/http_binding_test.cpp::http-moving-call-releases-a-moved-body, test:test/kafka_binding_test.cpp::kafka-from-message-moving-agrees-with-from-message, test:test/kafka_binding_test.cpp::kafka-moving-call-releases-a-moved-body, test:test/moving_in_v4_test.cpp::moving-entry-points-exist-in-v4, test:test/nats_binding_test.cpp::nats-from-message-moving-agrees-with-from-message, test:test/nats_binding_test.cpp::nats-moving-call-releases-a-moved-body, test:test/v3/v3_moving_absent_test.cpp::moving-entry-points-are-absent-from-the-frozen-generations]
owner: filip.sajdak
version: 2
---
The binding core's `read_body_moving` and each binding's `from_message_moving` shall return a result equal to the one the copying entry point returns for an equal message and equal decode options, and shall move the body of a binary-mode message into the `ce::binary` payload without copying it when the body is non-empty and its media type is absent or is not one `is_json_content_type` accepts. Any other body, including a JSON, structured or batch body and an empty body, shall take the copying path.
