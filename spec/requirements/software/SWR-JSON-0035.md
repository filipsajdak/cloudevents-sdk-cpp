---
uid: SWR-JSON-0035
title: An extension integer beyond int64 is refused end to end
type: software
status: approved
priority: high
rationale: >
  SWR-JSON-0033 states the codec's obligation; this states the one a user observes. Before it held, a document carrying 18446744073709551615 as an extension decoded successfully into an event whose extension value was -1, with no error anywhere in the chain.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::extension-integer-beyond-int64-is-refused]
owner: filip.sajdak
version: 1
---
When a JSON document carries an extension attribute whose integer value cannot be
represented, the json_format decoder shall return an error rather than an event,
and shall not produce an extension holding a different number.
