---
uid: SWR-EXT-0003
title: Typed extensions recover their types from a lossy decoder
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 and 5.4 both note that the JSON and HTTP binary wire forms lose the
  CloudEvents attribute type, leaving values as strings. SPEC 5.5 makes the typed
  extension structs the place where the declared type is restored, so a tracing
  extension survives a round trip with its types intact.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/extensions_test.cpp::extensions-type-recovery]
owner: filip.sajdak
version: 1
---
When an extension attribute has been decoded into its string form by a wire format
that carries no type information, the SDK shall convert that value to the field type
declared by the extension struct, reporting a typed error when the conversion fails.
