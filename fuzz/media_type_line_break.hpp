#pragma once

/// \file
/// \brief What a fuzz target checks of every event it decodes: its `datacontenttype` holds no CR
/// or LF (SWR-CORE-0041), so no binding can write a line break into a content-type field.

#include <string_view>

#include <cloudevents/core.hpp>

namespace ce_fuzz {

/// The characters SWR-CORE-0041 refuses in a `datacontenttype`.
inline constexpr std::string_view media_type_line_breaks = "\r\n";

/// Traps when `decoded` declares a `datacontenttype` containing CR or LF.
inline void check_media_type_has_no_line_break(const ce::event& decoded) {
  const auto& media_type = decoded.datacontenttype();
  if (media_type &&
      media_type->view().find_first_of(media_type_line_breaks) != std::string_view::npos) {
    __builtin_trap();
  }
}

}  // namespace ce_fuzz
