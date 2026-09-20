/// \file
/// \brief Every codec must be correct before it is allowed to be fast.
///
/// A benchmark of codecs that disagree measures nothing. This runs each one
/// through the CloudEvents rules a codec is actually load-bearing for, and the
/// benchmark refuses to report a codec that fails here.

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>

#include "codecs/boost_json_codec.hpp"
#include "codecs/glaze_codec.hpp"
#include "codecs/rapidjson_codec.hpp"
#include "documents.hpp"

#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, std::string_view codec, std::string_view what) {
  if (!ok) {
    std::printf("  FAIL  %-16s %s\n", std::string{codec}.c_str(), std::string{what}.c_str());
    ++failures;
  }
}

template <class C>
void run(std::string_view name) {
  using format = ce::json_format<C>;
  static_assert(ce::json::json_codec<C>);

  // --- the documents decode, and to the same event every codec sees --------
  auto minimal = format::decode(ce::bench::minimal_document);
  check(minimal.has_value(), name, "minimal document does not decode");
  if (minimal) {
    check(minimal->id == "A234-1234-1234", name, "minimal id");
    check(minimal->type == "com.example.someevent", name, "minimal type");
    check(minimal->validate().has_value(), name, "minimal does not validate");
  }

  auto full = format::decode(ce::bench::full_document);
  check(full.has_value(), name, "full document does not decode");
  if (!full) {
    return;
  }
  check(full->subject.has_value(), name, "subject lost");
  check(full->time.has_value(), name, "time lost");
  if (full->time) {
    // Nanosecond precision and a non-UTC offset both survive, which is what
    // SWR-CORE-0009 requires and what a codec can quietly ruin.
    check(ce::to_string(*full->time) == "2026-09-20T12:34:56.123456789+02:00", name,
          "timestamp did not round-trip byte for byte");
  }
  check(std::holds_alternative<ce::json_text>(full->data), name, "payload is not json_text");

  // --- the Integer / floating distinction ---------------------------------
  //
  // The single most common way a generic JSON value breaks CloudEvents: store
  // every number as a double and 30 becomes 30.0, which the format layer must
  // then refuse as a fractional extension value.
  const auto* rate = full->extension("sampledrate");
  check(rate != nullptr, name, "sampledrate extension lost");
  if (rate != nullptr) {
    check(std::holds_alternative<std::int32_t>(*rate), name,
          "sampledrate is not an Integer: this codec cannot distinguish 30 from 30.0");
    if (std::holds_alternative<std::int32_t>(*rate)) {
      check(std::get<std::int32_t>(*rate) == 30, name, "sampledrate value wrong");
    }
  }

  auto fractional = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","rate":1.5})");
  check(!fractional.has_value(), name, "a fractional extension value must be refused");
  if (!fractional) {
    check(fractional.error().code == ce::errc::type_mismatch, name,
          "a fractional extension should be a type_mismatch");
  }

  // --- absent is not null -------------------------------------------------
  //
  // The reason find() returns a pointer. A codec that cannot tell them apart
  // breaks the data rules.
  auto null_extension = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","gone":null,"kept":"x"})");
  check(null_extension.has_value(), name, "a null attribute must decode as unset");
  if (null_extension) {
    check(null_extension->extension("gone") == nullptr, name, "null extension became an attribute");
    check(null_extension->extension("kept") != nullptr, name, "a live extension was dropped");
  }

  auto conflict = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":1,"data_base64":"AA=="})");
  check(!conflict.has_value(), name, "both data members present must be refused");

  // --- round trips --------------------------------------------------------
  auto encoded = format::encode(*full);
  check(encoded.has_value(), name, "full event does not encode");
  if (encoded) {
    auto again = format::decode(*encoded);
    check(again.has_value(), name, "re-encoded document does not decode");
    if (again) {
      check(*again == *full, name, "round trip is not identity");
    }
  }

  auto batch = format::decode_batch(ce::bench::batch_document());
  check(batch.has_value(), name, "batch does not decode");
  if (batch) {
    check(batch->size() == 100, name, "batch lost events");
  }

  auto large = format::decode(ce::bench::large_document());
  check(large.has_value(), name, "large document does not decode");

  // --- UTF-8 --------------------------------------------------------------
  //
  // Non-BMP characters arrive as a surrogate PAIR from some producers, and a
  // codec that encodes each half separately produces invalid UTF-8.
  std::string escaped = R"({"specversion":"1.0","id":"1","source":"/s","type":"t","subject":")";
  escaped += static_cast<char>(92);
  escaped += "uD83D";
  escaped += static_cast<char>(92);
  escaped += "uDE00";
  escaped += R"("})";
  auto emoji = format::decode(escaped);
  check(emoji.has_value(), name, "a surrogate pair does not decode");
  if (emoji && emoji->subject) {
    check(emoji->subject->size() == 4, name,
          "a surrogate pair did not become one code point (CESU-8 or a dropped character)");
  }
}

}  // namespace

int main() {
  std::printf("codec correctness\n");
  run<ce::codec::nlohmann_codec>("nlohmann");
  run<ce::bench::rapidjson_codec>("rapidjson");
  run<ce::bench::boost_json_codec>("boost.json");
  run<ce::bench::glaze_codec>("glaze");

  if (failures == 0) {
    std::printf("  all codecs agree\n");
  } else {
    std::printf("\n%d check(s) failed\n", failures);
  }
  return failures == 0 ? 0 : 1;
}
