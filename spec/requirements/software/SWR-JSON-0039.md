---
uid: SWR-JSON-0039
title: The v3 codec concept requires value equality, a deep copy and an identity
type: software
status: reviewed
priority: high
rationale: >
  The encoder copies a retained DOM into its output document, and document equality needs the codec's own comparison.
  RapidJSON's value cannot be copy-constructed, so the codec supplies the deep copy the encoder needs rather than the concept requiring a copyable value.
  A json_document selects its fast path by comparing the identities of two codecs by value.
  An address-based tag can be merged by MSVC's default /OPT:ICF, which "can cause the same address to be assigned to different functions or read-only data members" (learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations), so the codec declares its identity as a string instead.
  RTTI was declined by the owner.
  CR-0003 requires all three of every v3 codec, so each operation has one code path.
  The v1 and v2 concepts are unchanged, so a codec written for them keeps working there.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The v3 `json_codec` concept shall require a codec to provide `equal(const value&, const value&) -> bool`, `copy(const value&) -> value` and a `static constexpr std::string_view identity`.
