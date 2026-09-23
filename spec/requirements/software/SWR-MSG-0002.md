---
uid: SWR-MSG-0002
title: raw_headers is constructible from a braced list of fields
type: software
status: implemented
delivered_in: v0.4.0
priority: medium
rationale: >
  A test or a caller describing a message that arrived on the wire states a list
  of fields, and building one by repeated `add` calls separates the field names
  from the shape they form. A literal list states the same thing in one
  expression, which is what makes a wrong field visible on inspection. The
  constructor belongs to the permissive type: keeping a repeated name is the
  point, and the invariant-holding type exists to make that state impossible.
verification_method: test
security_classification: operational
derived_from: [SYS-MSG-0001]
satisfied_by: [code:include/cloudevents/message.hpp]
verified_by: [test:test/binding_core_test.cpp::message-headers-from-field-list]
owner: filip.sajdak
version: 2
---
The `raw_headers` type shall be constructible from a braced list of name and
value pairs, keeping the order given and keeping a repeated name more than once.
