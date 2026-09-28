---
uid: SWR-BUILD-0013
title: The v0.6.0 API is ce::v4, and ce::v3 keeps what v0.5.0 published
type: software
status: reviewed
priority: high
rationale: >
  v0.5.0 published json_document with copy operations only and documented that moving a document copies it.
  CR-0004 gives the document move operations whose source reads as JSON null, which changes that documented behaviour.
  The owner ruled on 2026-09-28 that such a change is a break, so SWR-BUILD-0006 and SPEC section 3 rule 4 put it in a new namespace and keep the old one.
  The v0.5.0 suites are the evidence that ce::v3 keeps its behaviour as well as its declarations, as the v0.3.0 and v0.4.0 suites are for ce::v1 and ce::v2.
  ADR-0012 applies the layout ADR-0009 set for v1 and ADR-0010 applied to v2.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall declare its API in `ce::inline v4`, keep every entity that v0.5.0 published reachable as `ce::v3::X` with its v0.5.0 declaration, and run the v0.5.0 suites against `ce::v3`.
