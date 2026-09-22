---
uid: SWR-CORE-0016
title: Extension attribute accessors on event
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 lists `set_extension` and `extension` as the supported way to reach
  extension attributes, so callers do not depend on the map member and lookup of
  an absent name has a defined result rather than inserting a value. Under
  ADR-0008 the name argument is an `extension_name`, which has already refused an
  invalid or reserved name, so storing one can no longer fail. With the map no
  longer public, removal needs an accessor of its own, and it reports whether the
  name was present because a caller clearing an optional extension may need to know.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-extension-accessors]
owner: filip.sajdak
version: 3
---
The `event` type shall provide `set_extension`, taking an `extension_name` and an
`attribute_value` and storing it without a failure path, `remove_extension`,
reporting whether the named extension was present, and `extension`, returning the
stored value or an empty result when the name is absent.
