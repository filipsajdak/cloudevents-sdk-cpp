# A target that runs clang-tidy over the headers.
#
# .clang-tidy has existed since the scaffold, with WarningsAsErrors: '*' and a
# curated check set whose every exclusion names a reason. Nothing ran it. A lint
# configuration that no job invokes is a statement of intent, not a gate, and the
# tree drifted past it unnoticed.
#
# The library is header-only, so there are no translation units of its own to
# lint. The suites are the translation units that include every header, and
# CMAKE_EXPORT_COMPILE_COMMANDS gives clang-tidy the real flags they are built
# with - the same reason the warning-scope probe refuses try_compile.
# HeaderFilterRegex in .clang-tidy keeps the findings to include/cloudevents.

find_program(CE_CLANG_TIDY NAMES clang-tidy)

if(NOT CE_CLANG_TIDY)
  add_custom_target(tidy
    COMMAND "${CMAKE_COMMAND}" -E echo
      "clang-tidy was not found; install it or set CE_CLANG_TIDY to run this target"
    COMMAND "${CMAKE_COMMAND}" -E false)
  return()
endif()

# Lint the suites' sources: each includes the headers it exercises, so between
# them every public header is covered, with the flags it is really compiled with.
file(GLOB CE_TIDY_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/*.cpp")

add_custom_target(tidy
  COMMAND "${CE_CLANG_TIDY}"
          -p "${CMAKE_BINARY_DIR}"
          --warnings-as-errors=*
          ${CE_TIDY_SOURCES}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Running clang-tidy over the public headers"
  VERBATIM)
