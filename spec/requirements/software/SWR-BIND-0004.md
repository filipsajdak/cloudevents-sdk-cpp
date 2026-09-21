---
uid: SWR-BIND-0004
title: The HTTP binding keeps its behaviour on the shared core
type: software
status: approved
priority: high
rationale: >
  The extraction is worth nothing if it changes what the HTTP binding does. Error precedence is the part most at risk: a malformed field value is reported before the spec-version check and before the required-triple check, so reordering those changes which error a malformed message returns, and errors_test asserts the `where` string.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::binary-mode-ce-headers, test:test/http_binding_test.cpp::from-message-roundtrip]
owner: filip.sajdak
version: 1
---
The HTTP binding shall produce and accept the same messages after being expressed on
the shared core as before, including the order attributes are emitted in and the
order errors are reported in.
