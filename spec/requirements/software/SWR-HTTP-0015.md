---
uid: SWR-HTTP-0015
title: A decoded message always validates
type: software
status: approved
priority: high
rationale: >
  A header name is not constrained to the CloudEvents attribute grammar, so a sender may present ce- followed by anything an HTTP framework admits. Carrying that through as an extension would produce an event that to_message then refuses, breaking the round trip the binding exists to provide.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::decoded-message-always-validates]
owner: filip.sajdak
version: 1
---
When from_message would return an event that `validate()` rejects, it shall instead return an error result carrying that validation error. Where the cause is a ce- prefixed header whose remaining name does not match the pattern `[a-z0-9]+`, the error code shall be invalid_attribute_name and the error location shall name the attribute.
