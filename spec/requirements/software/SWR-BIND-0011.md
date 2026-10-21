---
uid: SWR-BIND-0011
title: The decode options carry an opt-in to skip the binary-mode parse, defaulting to the parse
type: software
status: approved
priority: medium
rationale: >
  CR-0005 reverses the owner's ruling of 2026-10-11 and adds an option to skip the parse of a binary-
  mode body, because a forwarder that never reads the payload pays that parse (CHANGELOG v0.6.0, known
  limitations). The option is one trailing member of the shared decode_options, and the default stays
  today's behaviour, so no call changes. A member with a default member initializer keeps every
  designated, positional and empty initializer compiling without a warning under -Wextra -Werror
  (checked on GCC 16, Apple clang 21 and clang 23 in CR-0005). The owner ruled on 2026-10-21 (CR-0005) that appending this member is an addition, so it needs no new version namespace.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/format/decode_options.hpp]
verified_by: [test:test/binding_core_test.cpp::decode-options-parse-binary-body-defaults-to-true, test:test/binding_core_test.cpp::decode-options-designated-initializer-still-builds]
owner: filip.sajdak
version: 1
---
The decode options shall declare, after `retain_document_up_to`, a `bool` member `parse_binary_body` whose default member initializer is `true`, so that an initializer naming only `retain_document_up_to` is valid and leaves `parse_binary_body` true.
