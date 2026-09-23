---
uid: SWR-JSON-0042
title: A json_document from another codec is converted through text
type: software
status: reviewed
priority: medium
rationale: >
  An event decoded with one codec may be encoded or read with another.
  Refusing it would make the codec a hidden property of every event.
  Converting through the building codec's serialisation is correct for every pair, and fast only for a matching one.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When the SDK uses a json_document with a codec other than the one that built it, the SDK shall convert the document by serialising it with the building codec and parsing the result with the codec in use.
