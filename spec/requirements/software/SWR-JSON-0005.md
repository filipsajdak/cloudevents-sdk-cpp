---
uid: SWR-JSON-0005
title: Codec member lookup and traversal
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 lists find, for_each_member and for_each_element as the read surface the decoder uses to locate known attributes and to walk unknown members and batch entries.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::json-codec-find-and-traversal]
owner: filip.sajdak
version: 1
---
The json_codec concept shall demand a find operation that locates a named member of an object value, a for_each_member operation that visits every name and value pair of an object value, and a for_each_element operation that visits every element of an array value.
