---
uid: SWR-BIND-0001
title: A binding is described by a traits type
type: software
status: approved
priority: high
rationale: >
  A traits type rather than function parameters, because attribute_prefix and case_sensitive_names are needed in constant expressions, and a std::function would allocate and add an indirect call per field in a header-only SDK that supports -fno-exceptions. The concept is what turns a missing or wrongly typed member into a diagnostic at the binding, rather than a template error inside the core.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-traits]
owner: filip.sajdak
version: 1
---
The SDK shall define a `binding_traits` concept requiring `attribute_prefix`,
`content_type_header`, `case_sensitive_names`, `encode_value` and `decode_value`, and
the shared binding functions shall accept only a type satisfying it.
