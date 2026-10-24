---
uid: SWR-PROTO-0016
title: Protobuf bytes interoperate with the Go and Java SDKs in both directions
type: software
status: reviewed
priority: high
rationale: >
  The evidence pattern of SWR-SEC-0008 and D-INTEROP-5: bytes recorded offline from the SDKs, a
  generator and verifier committed with them, no broker and no network in a test. The spike
  (pull request 113) recorded 13 events from sdk-go v2.15.2 and from sdk-java 2.5.0 and 13 hand-
  built cases from protoc. Differences between the SDKs are recorded as decisions and the
  goldens are not edited to hide them.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall decode the committed Protobuf bytes recorded from the Go and Java SDKs and from
protoc to the events those producers encoded, shall produce bytes that the Go and Java SDKs
decode to the event this SDK encoded, and shall commit the generators, the recorded bytes and
the verifiers together; a difference between the SDKs shall be recorded as a decision in
`docs/DECISIONS.md`.
