---
uid: SWR-MSG-0004
title: A repeated attribute field is refused rather than resolved
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  `headers::find` returns the first field of a name while `read_attributes`
  iterates every field and lets the last win, so a message carrying `ce-id` twice
  decoded differently from how the content-mode detection read it. The HTTP
  binding specification section 3.1.3 makes a repeated attribute field malformed,
  and picking either one silently is a guess about which peer was right.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-MSG-0001]
satisfied_by: [code:include/cloudevents/message.hpp, code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/message_headers_test.cpp::headers-adopt-refuses-a-repeated-attribute]
owner: filip.sajdak
version: 1
---
When two fields carry the same CloudEvents attribute, the conversion adopting
them shall return a failed result naming the repeated attribute.
