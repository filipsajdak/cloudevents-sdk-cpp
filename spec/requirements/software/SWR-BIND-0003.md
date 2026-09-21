---
uid: SWR-BIND-0003
title: One flag decides field-name case in both directions
type: software
status: approved
priority: high
rationale: >
  A binding that lowered a field name while reading but preserved it while writing would not round-trip, and the two halves are far enough apart in the code that the pair could be set inconsistently. Deriving both from one traits member removes the inconsistent state rather than testing for it. HTTP field names are case-insensitive (RFC 9110 section 5.1); Kafka record headers, AMQP application-properties and MQTT user properties are not.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-name-case]
owner: filip.sajdak
version: 1
---
Where a binding declares `case_sensitive_names`, the shared core shall use that one
value to decide prefix matching, attribute-name normalisation, content-type lookup
and whether writing a field replaces a differently-cased one.
