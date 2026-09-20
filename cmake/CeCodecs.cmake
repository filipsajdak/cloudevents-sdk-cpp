# The JSON codecs this build ships.
#
# The format layer is codec-plural by construction: json_format<Codec> names only
# Codec:: statics, and mini_codec exists to prove it (ADR-0004). The BUILD was
# codec-singular, keyed on one boolean, which is what this file replaces.

set(CE_KNOWN_CODECS nlohmann rapidjson)

foreach(codec IN LISTS CE_CODECS)
  if(NOT codec IN_LIST CE_KNOWN_CODECS)
    message(FATAL_ERROR
      "ce: CE_CODECS names '${codec}', which is not a codec this SDK ships. "
      "Known codecs: ${CE_KNOWN_CODECS}. To use a JSON library the SDK does not "
      "ship, supply a type satisfying ce::json::json_codec and link "
      "ce::format_json directly - the concept is the seam, not a fixed list.")
  endif()
endforeach()
list(REMOVE_DUPLICATES CE_CODECS)

# A per-codec boolean, so a guard reads CE_CODEC_NLOHMANN rather than a list
# membership test at every site.
foreach(codec IN LISTS CE_KNOWN_CODECS)
  string(TOUPPER ${codec} upper)
  set(CE_CODEC_${upper} OFF)
endforeach()
foreach(codec IN LISTS CE_CODECS)
  string(TOUPPER ${codec} upper)
  set(CE_CODEC_${upper} ON)
endforeach()

# Say what was resolved. CE_DEFAULT_CODEC subtracts from CE_CODECS rather than
# setting it, so a surprising combination should be visible rather than inferred.
if(CE_CODECS)
  message(STATUS "ce: codecs = ${CE_CODECS}")
else()
  message(STATUS "ce: codecs = (none); ce::format_json still builds")
endif()

# One codec target, built the same way whichever JSON library is behind it.
#
# DEPENDS is linked for the BUILD ONLY. install(EXPORT) refuses a target that
# links something outside the export set, and FetchContent makes a dependency a
# REAL target rather than an IMPORTED one - which is why the nlohmann case only
# ever reproduced on a machine WITHOUT nlohmann installed, where find_package
# does not win. Consumers get the link back from cloudeventsConfig.cmake.
#
# Do not "simplify" the generator expression away for a dependency that looks
# always-imported: a superproject that adds it by add_subdirectory makes it real.
function(ce_add_codec name)
  cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})

  add_library(ce_codec_${name} INTERFACE)
  add_library(ce::codec_${name} ALIAS ce_codec_${name})
  # EXPORT_NAME is not cosmetic: install(EXPORT ... NAMESPACE ce::) prepends the
  # namespace to the RAW target name, so without this the installed package
  # offers ce::ce_codec_${name}.
  set_target_properties(ce_codec_${name} PROPERTIES EXPORT_NAME codec_${name})

  set(build_only "")
  foreach(dependency IN LISTS ARG_DEPENDS)
    list(APPEND build_only "$<BUILD_INTERFACE:${dependency}>")
  endforeach()
  target_link_libraries(ce_codec_${name} INTERFACE ce_format_json ${build_only})

  set_property(GLOBAL APPEND PROPERTY CE_CODEC_TARGETS ce_codec_${name})
endfunction()

# The enabled codec targets, in CE_CODECS order.
function(ce_codec_targets out)
  get_property(targets GLOBAL PROPERTY CE_CODEC_TARGETS)
  set(${out} "${targets}" PARENT_SCOPE)
endfunction()
