---
uid: SWR-JSON-0043
title: A payload kept as text is the input's own text
type: software
status: approved
priority: high
rationale: >
  Above the retention limit the decoder produced json_text by serialising the parsed data member with the codec.
  In the CI perf job for PR #59 that serialisation cost decode_large 25 to 49 percent more instructions and 58 to 210 percent more allocated bytes than main, where the DOM is moved out.
  The input already holds the payload, so copying its bytes costs one allocation and no serialisation.
  The decoder finds those bytes with a structural scanner of its own, run on the input after the codec has parsed it, so the scanner only reads well-formed JSON.
  It is linear, skips strings with their escapes, tracks nesting depth with a counter rather than recursion, and allocates nothing, so hostile input cannot exhaust the stack or the heap through it.
  The value is located unambiguously only where the scanner can be certain that its slice is the member the codec decoded; otherwise the decoder stores the codec's serialisation instead.
  That is the case for a top-level member name written with an escape, which could spell data; for a duplicate top-level data member, since codecs differ in which duplicate they keep; and for anything a strict JSON reader would not expect.
  A batch applies the same rule to each element's own data member, and falls back per element.
  The stored text keeps the sender's spelling, so two events carrying the same JSON formatted differently no longer compare equal as json_text after decode.
  A scanner rather than a codec capability, because it works identically for every codec: nlohmann exposes positions only behind a build option, and neither Boost.JSON nor Glaze keeps them.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp, code:include/cloudevents/format/detail/json_slice.hpp]
verified_by: [test:test/json_format_test.cpp::decode-keeps-the-payloads-own-text]
owner: filip.sajdak
version: 1
---
When the json_format decoder keeps a JSON payload as json_text and it locates the data member's value in the input unambiguously, it shall store that value's bytes exactly as they appear in the input, without surrounding whitespace.
