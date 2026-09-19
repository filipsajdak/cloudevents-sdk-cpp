---
uid: SWR-CORE-0016
title: Extension attribute accessors on event
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 lists `set_extension` and `extension` as the supported way to reach
  extension attributes, so callers do not depend on the map member and lookup of
  an absent name has a defined result rather than inserting a value.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-extension-accessors]
owner: filip.sajdak
version: 1
---
The `event` type shall provide `set_extension(name, attribute_value)` storing an
extension attribute and `extension(name)` returning the stored value or an empty
result when the name is absent.
