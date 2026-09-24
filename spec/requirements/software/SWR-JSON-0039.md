---
uid: SWR-JSON-0039
title: The v3 codec concept requires value equality, a deep copy, a member move and an identity
type: software
status: approved
priority: high
rationale: >
  The encoder copies a retained DOM into its output document, and document equality needs the codec's own comparison.
  RapidJSON's value cannot be copy-constructed, so the codec supplies the deep copy the encoder needs rather than the concept requiring a copyable value.
  The decoder owns the document it parsed, so it can move the data member out instead of copying it.
  Measured on 2026-09-24 against dumping the member to text, a deep copy took up to 187 percent longer (Boost.JSON, a 414-byte event) and raised allocations from 13 to 4015 (nlohmann, a 52889-byte event); moving costs neither, because it allocates nothing and visits no node.
  The concept cannot move out through find, which returns a pointer to const, so the codec supplies the move as extract.
  Calling extract for an absent member is a precondition violation, and the codec returns a null value for it rather than undefined behaviour.
  A json_document selects its fast path by comparing the identities of two codecs by value.
  An address-based tag can be merged by MSVC's default /OPT:ICF, which "can cause the same address to be assigned to different functions or read-only data members" (learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations), so the codec declares its identity as a string instead.
  RTTI was declined by the owner.
  CR-0003 requires all four of every v3 codec, so each operation has one code path.
  The v1 and v2 concepts are unchanged, so a codec written for them keeps working there.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp, code:include/cloudevents/codec/nlohmann.hpp, code:include/cloudevents/codec/boost_json.hpp, code:include/cloudevents/codec/rapidjson.hpp]
verified_by: [test:test/json_codec_test.cpp::json-codec-requires-equal-copy-and-identity]
owner: filip.sajdak
version: 2
---
The v3 `json_codec` concept shall require a codec to provide `equal(const value&, const value&) -> bool`, `copy(const value&) -> value`, `extract(value& object, std::string_view key) -> value` and a `static constexpr std::string_view identity`.
`extract` shall move the named member out of `object` and leave `object` a valid value.
Its precondition is that `find(object, key)` returns a member; when it does not, `extract` shall return a null value and leave `object` unchanged.
