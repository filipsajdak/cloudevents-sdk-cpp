---
uid: SWR-JSON-0044
title: The encoder hands a codec the strings it would otherwise discard
type: software
status: implemented
delivered_in: v0.6.0
priority: medium
rationale: >
  The encoder builds some strings itself and drops each one as soon as the codec has made a value from it: the time attribute and a timestamp extension rendered as RFC 3339 text, and a binary extension or a data_base64 payload rendered as base64.
  make_string takes a std::string_view, so a codec whose value holds a std::string copies every one of them.
  nlohmann's value can take over a std::string, so handing it the string saves one allocation whenever the string is longer than the small-string buffer.
  The expected effect in the CI perf job is one allocation fewer for encode_full/nlohmann and about 100 fewer for encode_batch_100/nlohmann: the full event's time is 35 characters, and the batch's 20-character time is longer than libstdc++'s 15-character buffer.
  The member is optional, so the v3 concept does not change and a codec written for v0.5.0 keeps compiling and keeps the copy.
  It has its own name rather than being a make_string overload on std::string&&, for two reasons.
  A string literal argument would be ambiguous between the two overloads, which breaks callers of the published make_string, such as C::make_string("text") in the codec suites.
  And a std::string rvalue also converts to std::string_view, so a requires expression calling make_string could not tell whether a codec provides the overload.
  Boost.JSON and RapidJSON keep strings in their own storage and cannot take over a std::string, so their codecs do not provide it.
  The owner decided this on 2026-09-27.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp, code:include/cloudevents/format/json_format.hpp, code:include/cloudevents/codec/nlohmann.hpp]
verified_by: [test:test/json_format_test.cpp::encode-hands-built-strings-to-the-codec]
owner: filip.sajdak
version: 1
---
Where a codec provides `adopt_string` callable with a `std::string` rvalue and returning its `value`, the json_format encoder shall move into it each string the encoder built itself and discards after the call, instead of passing a view of that string to `make_string`.
