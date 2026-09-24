---
uid: SWR-CORE-0033
title: Two json_documents compare as JSON values
type: software
status: approved
priority: medium
rationale: >
  Event equality must not depend on how the payload was spelled on the wire, or on which codec parsed it.
  The same JSON must compare equal whatever codec parsed it.
  The codec that built both documents knows its own value equality.
  Documents from different codecs have no shared DOM, so the left-hand codec parses the right-hand document's serialisation and compares with its own `equal`.
  Comparing compact serialisations instead would report two documents differing only in member order as unequal.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/json_document_test.cpp::json-document-compares-as-json]
owner: filip.sajdak
version: 1
---
When two `json_document` values are compared, the comparison shall report the codec's `equal` result if one codec built both, and otherwise the `equal` result of the left-hand codec on the right-hand document parsed from its serialisation.
