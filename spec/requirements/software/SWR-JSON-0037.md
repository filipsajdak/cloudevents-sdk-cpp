---
uid: SWR-JSON-0037
title: One set of format rules for shipped and measured codecs alike
type: software
status: approved
priority: medium
rationale: >
  A benchmark of codecs that disagree measures nothing, so bench/codec_check.cpp had
  its own copy of the format rules. Two copies drift, and this pair already had: a
  timestamp surviving the format byte for byte with its offset, and a hundred-event
  batch, were checked only in the benchmark's copy, which no CI job runs. A codec that
  can only be measured cannot live in the test tree, because Glaze needs C++23 and the
  library floor is C++20, so the rules move to a header both callers include rather
  than to a test only one of them can run.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:test/json_format_checks.hpp]
verified_by: [test:test/json_format_test.cpp::format-rules-shared-with-the-bench]
owner: filip.sajdak
version: 1
---
The SDK shall express the format rules that every codec must satisfy once, in a form
both the test suite and the benchmark's correctness check run, so that a codec which
is only measured is judged by the same rules as one which ships.
