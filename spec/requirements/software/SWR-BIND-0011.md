---
uid: SWR-BIND-0011
title: The decode options carry an opt-in to skip the binary-mode parse, defaulting to the parse
type: software
status: reviewed
priority: medium
rationale: >
  CR-0005 reverses the owner's ruling of 2026-10-11 and adds an option to skip the parse of a binary-
  mode body, because a forwarder that never reads the payload pays that parse (CHANGELOG v0.6.0, known
  limitations). The option is one trailing member of the shared decode_options, and the default stays
  today's behaviour, so no call changes. A member with a default member initializer keeps every
  designated, positional and empty initializer compiling without a warning under -Wextra -Werror
  (checked on GCC 16, Apple clang 21 and clang 23 in CR-0005). The rule 4 analysis of CR-0005 argues
  that this is an addition; it is recorded there, and the owner confirms it before the requirement is
  approved.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The decode options shall declare, after `retain_document_up_to`, a `bool` member `parse_binary_body` whose default member initializer is `true`, so that an initializer naming only `retain_document_up_to` is valid and leaves `parse_binary_body` true.
