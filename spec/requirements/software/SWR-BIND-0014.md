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
  of the instructions at 256 B).
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The binding core's `read_body_moving` and each binding's `from_message_moving` shall return a result equal to the one the copying entry point returns for an equal message and equal decode options, and shall move a non-empty body under a media type other than JSON into the `ce::binary` payload without copying it.
