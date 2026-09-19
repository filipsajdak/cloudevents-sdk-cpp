---
uid: SWR-CORE-0015
title: event optional attributes and extension map
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 marks `datacontenttype`,
  `dataschema`, `subject` and `time` OPTIONAL, and SPEC 5.1 stores extension
  attributes in a transparently comparable ordered map so lookup by string_view
  needs no allocation.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-event-optional-attributes]
owner: filip.sajdak
version: 1
---
The `event` type shall carry optional members `datacontenttype`, `dataschema`,
`subject` and `time`, a `data` member of type `data_t`, and an `extensions`
member of type `std::map<std::string, attribute_value, std::less<>>`.
