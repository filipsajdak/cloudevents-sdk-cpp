/// \file
/// \brief Fuzz JSON event decoding, through both codecs.
///
/// This is the structured-mode receive path: a whole event document straight off
/// the wire.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/format/detail/json_slice.hpp>
#include <cloudevents/format/json_format.hpp>

#include "../test/mini_codec.hpp"

namespace {

constexpr ce::json::decode_options always_text{.retain_document_up_to = 0};

/// The payload kept as text above the retention limit is the input's own slice
/// of the data member where the scanner found one (SWR-JSON-0043). That slice
/// must be JSON the codec reads back as the very value the same input decodes
/// to as a document. The values are compared by their serialisations, because
/// mini_codec's equal is not reflexive for an object with a repeated key. Where
/// the scanner is unsure the text is the codec's own serialisation, which
/// mini_codec does not produce exactly for every double, so that path is not
/// compared.
template <class Codec>
void check_text_matches_document(std::string_view text) {
  using format = ce::json_format<Codec>;
  const auto as_text = format::decode(text, always_text);
  const auto as_document = format::decode(text);
  if (as_text.has_value() != as_document.has_value()) {
    __builtin_trap();
  }
  if (!as_text) {
    return;
  }
  const auto* kept = std::get_if<ce::json_text>(&as_text->data());
  const auto slice = ce::json::detail::data_member_text(text);
  if (kept == nullptr || !slice) {
    return;
  }
  if (kept->raw != *slice) {
    __builtin_trap();
  }
  const auto* document = std::get_if<ce::json_document>(&as_document->data());
  const auto* own = document == nullptr ? nullptr : document->template get<Codec>();
  const auto reparsed = Codec::parse(kept->raw);
  if (own == nullptr || !reparsed || Codec::dump(*reparsed) != Codec::dump(*own)) {
    __builtin_trap();
  }
}

/// The scanner also meets input no codec accepted: it must stay in bounds and
/// finish, whatever it is given.
void scan_without_a_codec(std::string_view text) {
  (void)ce::json::detail::data_member_text(text);
  ce::json::detail::batch_data_slices slices{text};
  for (std::size_t element = 0; element <= text.size(); ++element) {
    (void)slices.next();
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};

  using format = ce::json_format<ce::codec::nlohmann_codec>;
  auto decoded = format::decode(text);
  if (decoded) {
    // An event that decoded must re-encode, and the encoding must decode to the
    // same event: the decoder must not produce a value the encoder rejects or
    // rewrites (SWR-JSON-0031).
    auto encoded = format::encode(*decoded);
    if (!encoded) {
      __builtin_trap();
    }
    auto again = format::decode(*encoded);
    if (!again || !(*again == *decoded)) {
      __builtin_trap();
    }
  }

  // The second codec is not redundant: the two parse independently, so an input
  // one accepts and the other rejects is a disagreement worth finding.
  (void)ce::json_format<ce::test::mini_codec>::decode(text);
  (void)ce::json_format<ce::codec::nlohmann_codec>::decode_batch(text);

  check_text_matches_document<ce::codec::nlohmann_codec>(text);
  check_text_matches_document<ce::test::mini_codec>(text);
  (void)ce::json_format<ce::codec::nlohmann_codec>::decode_batch(text, always_text);
  (void)ce::json_format<ce::test::mini_codec>::decode_batch(text, always_text);
  scan_without_a_codec(text);
  return 0;
}
