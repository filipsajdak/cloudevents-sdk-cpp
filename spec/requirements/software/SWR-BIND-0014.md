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
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The binding core's `read_body_moving` and each binding's `from_message_moving` shall return a result equal to the one the copying entry point returns for an equal message and equal decode options, and shall move the body of a binary-mode message into the `ce::binary` payload without copying it when the body is non-empty and its media type is absent or is not one `is_json_content_type` accepts. Any other body, including a JSON, structured or batch body and an empty body, shall take the copying path.
