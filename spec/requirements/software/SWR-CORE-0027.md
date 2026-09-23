---
uid: SWR-CORE-0027
title: An invalid attribute literal fails to compile
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  A hardcoded `source` or `type` is known when the program is compiled, so
  checking it at run time spends work on a question already answerable. ADR-0008
  routes a literal through a consteval proxy, which also makes the failure a
  diagnostic at the offending line rather than an error value some caller may
  discard. The proxy exists because a consteval constructor cannot sit on a type
  owning a `std::string`, measured on GCC 16 and Clang 23.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp]
verified_by: [test:test/attribute_types_test.cpp::attribute-literals-are-checked-when-compiled]
owner: filip.sajdak
version: 1
---
When a literal offered as a context attribute fails the rule for that attribute,
the compile-time construction path shall reject the translation unit with a
diagnostic naming the rule that refused it.
