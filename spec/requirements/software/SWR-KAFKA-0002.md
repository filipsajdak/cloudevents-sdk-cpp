---
uid: SWR-KAFKA-0002
title: content-type carries datacontenttype and takes no prefix
type: software
status: approved
priority: high
rationale: >
  The Kafka binding specification section 3.2.2 states that in binary mode the `content-type` header MUST be mapped directly to the CloudEvents `datacontenttype` attribute. Emitting it under the prefix as well would present the same attribute twice, and a receiver has no rule for which one wins. CR-0010 added the exception: a datacontenttype that starts with application/cloudevents would make the header declare structured mode, so SWR-BIND-0019 refuses the event in binary mode instead. CR-0011 added another: binary data under a JSON media type would be parsed by the reader and fail, so SWR-BIND-0021 refuses that event too.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-content-type]
owner: filip.sajdak
version: 3
---
The Kafka binding shall carry datacontenttype in a record header named
`content-type` without the attribute prefix, and shall not also emit
`ce_datacontenttype`, for every event that neither SWR-BIND-0019 nor SWR-BIND-0021 refuses.
