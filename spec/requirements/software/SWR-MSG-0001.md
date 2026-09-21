---
uid: SWR-MSG-0001
title: Byte-exact header lookup alongside the case-insensitive one
type: software
status: approved
priority: high
rationale: >
  SWR-HTTP-0002 makes ce::headers match case-insensitively, which is correct for HTTP field names and over-permissive everywhere else. Kafka, AMQP and MQTT key comparison is byte-exact, so a binding using set would erase a differently-cased header a caller had deliberately kept, and find would answer with one the transport considers unrelated.
verification_method: test
security_classification: operational
derived_from: [SYS-MSG-0001]
satisfied_by: [code:include/cloudevents/message.hpp]
verified_by: [test:test/http_binding_test.cpp::headers-case-sensitive-lookup]
owner: filip.sajdak
version: 1
---
The `headers` type shall provide `find_exact`, `contains_exact` and `set_exact`,
which compare a field name byte for byte, alongside the case-insensitive `find`,
`contains` and `set`, and the case-insensitive behaviour of the existing three
shall be unchanged.
