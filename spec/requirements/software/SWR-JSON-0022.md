---
uid: SWR-JSON-0022
title: Unknown top-level members become extensions
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause treats every top-level member that is not a defined context attribute or a payload member as an extension attribute, as SPEC 5.3 restates.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::unknown-top-level-members-become-extensions]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a top-level member that is neither a defined context attribute nor a payload member, it shall record that member as an extension attribute of the decoded event.
