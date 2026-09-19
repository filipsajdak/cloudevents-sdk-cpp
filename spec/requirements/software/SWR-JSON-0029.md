---
uid: SWR-JSON-0029
title: base64 decode accepts missing padding
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.3 requires decode to accept missing padding, because producers in the wild omit the trailing equals signs and rejecting them would break interoperability.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/base64.hpp]
verified_by: [test:test/base64_test.cpp::base64-decode-accepts-missing-padding]
owner: filip.sajdak
version: 1
---
When base64 decode reads input whose trailing padding characters are absent, it shall decode the input successfully to the octets the present characters encode.
