# A target that runs clang-tidy over the headers, and fails on what it finds.
#
# The library is header-only, so there are no translation units of its own to
# lint. The suites are the translation units that include every header, and
# CMAKE_EXPORT_COMPILE_COMMANDS gives clang-tidy the real flags they are built
# with - the same reason the warning-scope probe refuses try_compile.
#
# tidy_gate.py runs them in parallel and keeps only the findings located in
# include/cloudevents: clang-tidy always reports the file it was given, so a
# direct run would also enforce the whole check set on the tests (D-TIDY-2).

find_program(CE_CLANG_TIDY NAMES clang-tidy)
find_package(Python3 COMPONENTS Interpreter)

if(NOT CE_CLANG_TIDY OR NOT Python3_Interpreter_FOUND)
  add_custom_target(tidy
    COMMAND "${CMAKE_COMMAND}" -E echo
      "clang-tidy and python3 are both needed; set CE_CLANG_TIDY to point at clang-tidy"
    COMMAND "${CMAKE_COMMAND}" -E false)
  return()
endif()

# The suites ce_add_test registered, so the probes that must not compile, and
# anything a disabled option left out, are never handed to clang-tidy.
get_property(CE_TIDY_SOURCES GLOBAL PROPERTY CE_TIDY_SOURCES)

add_custom_target(tidy
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/cmake/tidy_gate.py"
          "${CE_CLANG_TIDY}" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}"
          ${CE_TIDY_SOURCES}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Running clang-tidy over the public headers"
  VERBATIM)
