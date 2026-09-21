---
uid: SWR-JSON-0038
title: base64 padding must be exactly what the final quantum needs
type: software
status: approved
priority: high
rationale: >
  The decoder stripped any run of trailing padding before looking at the length,
  so several spellings decoded to one value. That is the same defect the
  unused-trailing-bits rule already guards against, and it has the same
  consequence: two peers comparing encoded forms disagree with two peers
  comparing decoded ones. RFC 4648 section 4 pads a final quantum out to four
  characters, which is at most two of them.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/base64.hpp]
verified_by: [test:test/base64_test.cpp::base64-decode-rejects-invalid-input]
owner: filip.sajdak
version: 1
---
If base64 input carries more than two padding characters, or carries padding that
does not complete its final quantum, the decoder shall return a failed result
reporting invalid base64.
