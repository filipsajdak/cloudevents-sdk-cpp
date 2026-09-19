---
uid: SWR-DESC-0009
title: Supported described member types
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 enumerates the member types the seam maps to JSON members, extension
  attributes and headers; fixing the list is what lets the JSON codec and the header
  mapper be written once over a closed set of shapes.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::supported-member-types]
owner: filip.sajdak
version: 1
---
The reflection seam shall support described members of type `bool`, an integral type, a
floating point type, `std::string`, `std::optional<T>`, `std::vector<T>`,
`std::map<std::string, T>`, a nested described type, and the core attribute types.
