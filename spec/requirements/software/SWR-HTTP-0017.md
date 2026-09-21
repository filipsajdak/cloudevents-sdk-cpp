---
uid: SWR-HTTP-0017
title: The percent-encoding policy validates on the way out as well as in
type: software
status: approved
priority: medium
rationale: >
  `literal_values::encode` refuses a control character and ill-formed UTF-8, while
  `percent_encoded_values::encode` validated nothing and emitted the bytes
  percent-encoded. The value then failed at the receiving peer's decode, which
  reports the fault to whoever did not commit it. Producing a message this SDK
  would itself refuse to read is a defect the producer should hear about.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_encoding_test.cpp::percent-encoding-refuses-ill-formed-utf8-on-encode]
owner: filip.sajdak
version: 1
---
When an attribute value offered to the percent-encoding policy is not well-formed
UTF-8, the encoder shall return a failed result naming the attribute.
