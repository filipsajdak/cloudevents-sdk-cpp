---
uid: SWR-CORE-0015
title: event optional attributes and extension map
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 marks `datacontenttype`,
  `dataschema`, `subject` and `time` OPTIONAL. ADR-0008 makes each of them a type
  that cannot hold a forbidden value and keys the extension map by
  `extension_name` for the same reason, so a map entry cannot carry a name the
  encoder would later refuse.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-event-optional-attributes]
owner: filip.sajdak
version: 2
---
The `event` type shall carry optional `datacontenttype`, `dataschema`, `subject`
and `time` attributes, a `data` member of type `data_t`, and an extension map
keyed by `extension_name`, each reachable through a const accessor.
