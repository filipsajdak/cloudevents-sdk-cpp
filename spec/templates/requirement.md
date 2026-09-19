---
uid: SWR-XXXX-0000
title: Short imperative title
type: software            # stakeholder | system | software - must match uid prefix and folder
status: draft             # draft -> reviewed -> approved -> implemented -> verified -> obsolete
priority: medium          # low | medium | high
rationale: >
  Why this requirement exists. Reference the driving need, stakeholder
  concern, regulation, or quality goal.
verification_method: test # test | analysis | inspection | demonstration
security_classification: operational  # allowed values come from schema/profiles.yml (active profile)
derived_from: []          # parent UIDs, exactly one level up (SWR -> SYS, SYS -> STK)
satisfied_by: []          # e.g. code:src-tauri/src/signing/embed.rs, doc:adr/0008.md
verified_by: []           # e.g. test:tests/signing_test.rs::softhsm, acc:scenario-G (path-qualify test refs so they link + validate); required once approved+
owner: firstname.lastname
version: 1
# delivered_in: v0.1.0    # optional: app release that delivered this (set once implemented/verified)
---
Write the requirement statement here using an EARS pattern, ending the
sentence with a single testable "shall" clause:

- Ubiquitous: The <system> shall <response>.
- Event-driven: When <trigger>, the <system> shall <response>.
- State-driven: While <state>, the <system> shall <response>.
- Unwanted behaviour: If <condition>, then the <system> shall <response>.
- Optional feature: Where <feature is included>, the <system> shall <response>.
