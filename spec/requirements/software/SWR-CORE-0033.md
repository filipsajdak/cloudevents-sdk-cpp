---
uid: SWR-CORE-0033
title: Two json_documents compare as JSON values
type: software
status: reviewed
priority: medium
rationale: >
  Event equality must not depend on how the payload was spelled on the wire, or on which codec parsed it.
  The codec that built both documents knows its own value equality.
  Documents from different codecs have no shared DOM, so their compact serialisations are compared instead.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When two `json_document` values are compared, the comparison shall report the codec's `equal` result if one codec built both, and the equality of their compact serialisations if different codecs built them.
