---
uid: SWR-PROTO-0018
title: The guide states the mapping and what it loses
type: software
status: reviewed
priority: medium
rationale: >
  A user chooses the format knowing which parts of an event survive it: the offset of a time,
  the digits of a fraction, a JSON classification of the data and the type URL of a proto_data
  payload (SWR-JSON-0023 does the same for JSON).
verification_method: inspection
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The guide `docs/GUIDE.md` shall have a section on the Protobuf format that states the entry
points, the field numbers and their source, the data field rules, the decode limits, and each
information loss of the mapping.
