---
uid: SWR-JSON-0039
title: The v3 codec concept requires value equality, a deep copy, a member move, mutable element traversal and an identity
type: software
status: implemented
delivered_in: v0.5.0
priority: high
rationale: >
  The encoder copies a retained DOM into its output document, and document equality needs the codec's own comparison.
  RapidJSON's value cannot be copy-constructed, so the codec supplies the deep copy the encoder needs rather than the concept requiring a copyable value.
  The decoder owns the document it parsed, so it can move the data member out instead of copying it.
  Measured on 2026-09-24 against dumping the member to text, a deep copy took up to 187 percent longer (Boost.JSON, a 414-byte event) and raised allocations from 13 to 4015 (nlohmann, a 52889-byte event); moving costs neither, because it allocates nothing and visits no node.
  The concept cannot move out through find, which returns a pointer to const, so the codec supplies the move as extract.
  A batch decoder owns its parsed array just as a single decoder owns its object, so it moves each element's data member out too, which needs a mutable reference to the element.
  for_each_element reaches elements only through a reference to const, so the codec supplies for_each_mutable_element.
  Copying instead cost decode_batch_100/boost.json 5.48 percent more instructions than main in the CI perf job on 2026-09-24 (2,418,617 to 2,551,208), and the other codecs 1.3 to 1.5 percent.
  The traversal has its own name rather than a for_each_element overload taking value&, because the in-tree const overloads accept any visitor, so a codec with only the const overload would satisfy a concept that asked for the mutable call and fail only inside the decoder.
  Calling extract for an absent member is a precondition violation, and the codec returns a null value for it rather than undefined behaviour.
  A json_document selects its fast path by comparing the identities of two codecs by value.
  An address-based tag can be merged by MSVC's default /OPT:ICF, which "can cause the same address to be assigned to different functions or read-only data members" (learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations), so the codec declares its identity as a string instead.
  RTTI was declined by the owner.
  CR-0003 requires all five of every v3 codec, so each operation has one code path.
  The v1 and v2 concepts are unchanged, so a codec written for them keeps working there.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp, code:include/cloudevents/format/json_format.hpp, code:include/cloudevents/codec/nlohmann.hpp, code:include/cloudevents/codec/boost_json.hpp, code:include/cloudevents/codec/rapidjson.hpp]
verified_by: [test:test/json_codec_test.cpp::json-codec-requires-equal-copy-and-identity, test:test/json_format_test.cpp::decode-batch-moves-each-payload]
owner: filip.sajdak
version: 3
---
The v3 `json_codec` concept shall require a codec to provide `equal(const value&, const value&) -> bool`, `copy(const value&) -> value`, `extract(value& object, std::string_view key) -> value` and a `static constexpr std::string_view identity`.
It shall also require `for_each_mutable_element(value& array, F visit) -> void`, which calls `visit` with a `value&` for each element of `array` in order; the `for_each_element` overload on a `const value&` remains.
`extract` shall move the named member out of `object` and leave `object` a valid value.
Its precondition is that `find(object, key)` returns a member; when it does not, `extract` shall return a null value and leave `object` unchanged.
