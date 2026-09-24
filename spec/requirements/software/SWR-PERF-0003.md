---
uid: SWR-PERF-0003
title: A pull request that makes a decoded event retain more than 1 percent more memory fails
type: software
status: reviewed
priority: high
rationale: >
  An event kept in a queue or a cache holds its memory for as long as it lives.
  CR-0003 lets an event keep a parsed document, which costs more than its text, so the bytes a decoded event retains are tracked per payload size.
  The measurement is exact; the 1 percent allowance absorbs a standard library changing its allocation rounding.
verification_method: demonstration
security_classification: operational
derived_from: [SYS-PERF-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a pull request increases the heap memory that a decoded event retains by more than 1 percent over main, the performance job shall fail.
