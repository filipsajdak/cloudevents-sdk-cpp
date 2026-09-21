---
uid: SWR-MSG-0002
title: headers is constructible from a braced list of fields
type: software
status: approved
priority: medium
rationale: >
  A test or a caller describing a message that arrived on the wire states a list
  of fields, and building one by repeated `add` calls separates the field names
  from the shape they form. A literal list states the same thing in one
  expression, which is what makes a wrong field visible on inspection.
verification_method: test
security_classification: operational
derived_from: [SYS-MSG-0001]
satisfied_by: [code:include/cloudevents/message.hpp]
verified_by: [test:test/binding_core_test.cpp::message-headers-from-field-list]
owner: filip.sajdak
version: 1
---
The `headers` type shall be constructible from a braced list of name and value
pairs, keeping the order given and keeping a repeated name more than once.
