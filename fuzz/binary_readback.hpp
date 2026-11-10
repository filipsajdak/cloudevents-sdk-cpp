#pragma once

/// \file
/// \brief What a fuzz target checks of a binary-mode write: it reads back, or it is refused for
/// exactly the reason SWR-BIND-0019 states.

#include <string_view>
#include <variant>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "media_type_line_break.hpp"

namespace ce_fuzz {

/// Whether binary mode must refuse `cloud_event`: its `datacontenttype`, after leading whitespace,
/// is one `content_mode_of` (the binding's reader test) reads as structured or batched.
[[nodiscard]] inline auto names_a_format(
    const ce::event& cloud_event, ce::content_mode (*content_mode_of)(std::string_view) noexcept)
    -> bool {
  const auto& media_type = cloud_event.datacontenttype();
  return media_type &&
         content_mode_of(ce::binding::detail::without_leading_whitespace(media_type->view())) !=
             ce::content_mode::binary_mode;
}

/// Binary data under a JSON media type: binary mode writes the bytes and the reader parses them as
/// JSON, so they read back only when they are JSON. The owner kept this gap out of CR-0010 and a
/// follow-up change request decides it (D-BIND-5); until then the write is checked and the read is
/// not. The seeds `binary_data_under_json_media_type` keep the shape in every run.
[[nodiscard]] inline auto binary_under_json(const ce::event& cloud_event) -> bool {
  const auto& media_type = cloud_event.datacontenttype();
  return std::holds_alternative<ce::binary>(cloud_event.data()) && media_type &&
         ce::is_json_content_type(media_type->view());
}

/// The attributes a substitution would change.
[[nodiscard]] inline auto same_identity(const ce::event& left, const ce::event& right) -> bool {
  return left.id() == right.id() && left.source() == right.source() &&
         left.type() == right.type() && left.datacontenttype() == right.datacontenttype();
}

/// Whether a binding may refuse a binary-mode write for a value its transport cannot carry (MQTT:
/// a string over 65,535 bytes or with NUL), not only for the media type.
enum class value_limits : bool { none, transport };

/// The attribute the SWR-BIND-0019 refusal names.
inline constexpr std::string_view media_type_attribute = "datacontenttype";

/// The SWR-BIND-0019 refusal: `invalid_argument` naming `datacontenttype`.
[[nodiscard]] inline auto is_format_refusal(const ce::error& failure) -> bool {
  return failure.code == ce::errc::invalid_argument && failure.where == media_type_attribute;
}

/// A transport-limit refusal: the value rules of the MQTT User Property writer (SWR-MQTT-0011) fail
/// with `invalid_argument` and name the attribute whose value broke them. The Content Type
/// property is written without those rules, so the attribute is never `datacontenttype`.
[[nodiscard]] inline auto is_transport_refusal(const ce::error& failure) -> bool {
  return failure.code == ce::errc::invalid_argument && !failure.where.empty() &&
         failure.where != media_type_attribute;
}

/// Traps unless `decoded` declares no `datacontenttype` with CR or LF (SWR-CORE-0041) and the
/// binary-mode `written` of `decoded` is the SWR-BIND-0019 refusal exactly when `names_a_format`
/// says so, otherwise a transport-limit refusal where `limits` allows one, and otherwise reads back
/// through `read` as the same event.
template<class Read>
void check_binary_write(const ce::event& decoded,
                        const ce::result<ce::message>& written,
                        ce::content_mode (*content_mode_of)(std::string_view) noexcept,
                        value_limits limits,
                        Read read) {
  check_media_type_has_no_line_break(decoded);
  if (names_a_format(decoded, content_mode_of)) {
    if (written || !is_format_refusal(written.error())) {
      __builtin_trap();
    }
    return;
  }
  if (!written) {
    if (limits == value_limits::none || !is_transport_refusal(written.error())) {
      __builtin_trap();
    }
    return;
  }
  if (binary_under_json(decoded)) {
    return;
  }
  const auto back = read(*written);
  if (!back || !same_identity(*back, decoded)) {
    __builtin_trap();
  }
}

}  // namespace ce_fuzz
