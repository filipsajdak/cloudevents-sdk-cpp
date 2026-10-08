---
uid: SWR-CORE-0040
title: to_v3 of a moved-from json_document yields a valid ce::v3 document
type: software
status: approved
priority: medium
rationale: >
  A ce::v4 json_document can be in the moved-from state of SWR-CORE-0035, and ce::v3 promised in v0.5.0 that every document holds a DOM a codec built.
  to_v3 is infallible and must return a valid ce::v3 event, so it has to give that payload some v3 form.
  The owner decided on 2026-09-29 on the form that needs no special case and no codec: the v3 document shares the static moved-from model, as SWR-CORE-0039 shares every other model, so it dumps null, yields its DOM to no codec and equals only another moved-from document.
  It is reachable only through to_v3, which v0.5.0 did not have, so no v0.5.0 program can observe it.
  The owner declined json_text holding null, which changes the payload's alternative, and an absent payload, which loses the fact that there was one; a v3 document holding a codec's null is not possible, because core names no codec to build it with.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/v3_conversion.hpp]
verified_by: [test:test/generation_conversion_test.cpp::to-v3-of-a-moved-from-document]
owner: filip.sajdak
version: 2
---
When `to_v3` converts an event whose payload is a moved-from `ce::v4::json_document`, it shall store a `ce::v3::json_document` that shares the moved-from model, whose `dump()` returns `null` and whose `get<Codec>()` returns `nullptr` for every codec.
