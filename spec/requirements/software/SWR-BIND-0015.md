---
uid: SWR-BIND-0015
title: After a successful moving call the message keeps its headers and its body is unspecified
type: software
status: reviewed
priority: medium
rationale: >
  A caller of a moving entry point gives up the body, and the contract says what remains: headers have
  no extraction interface, so they stay as they were, and the body is valid but unspecified, as a
  moved-from std::vector is. Promising more, for example an empty body, would bind the library to a
  move that clears it. A caller must not read the body afterwards.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a moving entry point returns a successful result, the message passed to it shall hold the headers it held before the call and a valid body whose contents are unspecified.
