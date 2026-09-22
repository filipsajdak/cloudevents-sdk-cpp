# One executable per component, each registered with CTest (SPEC section 6).
#
# ce_add_test keeps the per-suite boilerplate in one place so adding a suite is a
# single line and every suite gets the same warning set and the same ut linkage.

# The codec list, as a header every suite includes. Adding a codec to CE_CODECS
# adds it to every paired suite without editing one of them.
set(CE_CODEC_INCLUDES "")
set(CE_CODEC_VISITS "")
foreach(codec IN LISTS CE_CODECS)
  string(APPEND CE_CODEC_INCLUDES "#include <cloudevents/codec/${codec}.hpp>\n")
  string(APPEND CE_CODEC_VISITS
         "  visit.template operator()<::ce::codec::${codec}_codec>(std::string_view{\"${codec}_codec\"});\n")
endforeach()
configure_file(codecs_under_test.hpp.in
               "${CMAKE_CURRENT_BINARY_DIR}/gen/codecs_under_test.hpp" @ONLY)

function(ce_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ce::core ce::warnings Boost::ut ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# A suite that runs over every enabled codec, rather than over one named codec.
# The link line follows CE_CODECS, so the list and the binary cannot drift.
function(ce_add_codec_test name)
  ce_add_test(${name} ${ARGN})
  # The generated header names mini_codec.hpp, which lives beside the suites in
  # the source tree rather than next to the generated copy.
  target_include_directories(${name} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  ce_codec_targets(codec_targets)
  target_link_libraries(${name} PRIVATE ${codec_targets})
endfunction()

ce_add_test(config_test SOURCES config_test.cpp)
ce_add_test(core_test SOURCES core_test.cpp)
ce_add_test(build_test SOURCES build_test.cpp)
ce_add_test(static_error_test SOURCES static_error_test.cpp)
ce_add_test(attribute_types_test SOURCES attribute_types_test.cpp)
ce_add_test(describe_parity_test SOURCES describe_parity_test.cpp)
ce_add_test(base64_test SOURCES base64_test.cpp)
# The binding core names no codec, so this suite runs in a build with none.
ce_add_test(binding_core_test SOURCES binding_core_test.cpp)

ce_add_test(module_test SOURCES module_test.cpp)
# CE_MODULE_IN_DEFAULT_BUILD carries the option's value rather than a constant,
# so the assertion reads the real configuration: switching CE_BUILD_MODULE on
# makes the test fail, which is what proves it is off by default.
target_compile_definitions(module_test PRIVATE
  "CE_MODULE_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/../include/cloudevents/cloudevents.cppm\""
  "CE_INCLUDE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../include\""
  "CE_MODULE_IN_DEFAULT_BUILD=$<BOOL:${CE_BUILD_MODULE}>")

# These name nlohmann_codec directly, and without that codec in CE_CODECS the
# nlohmann target does not exist. Registering them unconditionally would make the
# no-default-codec preset fail to configure, which is the very build that proves
# SWR-JSON-0030. base64_test above has no codec dependency, so it stays.
if(CE_CODEC_NLOHMANN)
  ce_add_codec_test(json_codec_test
    SOURCES json_codec_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  # The isolation suite reads the headers, so it needs to know where they are.
  target_compile_definitions(json_codec_test
    PRIVATE "CE_INCLUDE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../include\"")
  ce_add_codec_test(json_format_test
    SOURCES json_format_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_codec_test(http_binding_test
    SOURCES http_binding_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_codec_test(kafka_binding_test
    SOURCES kafka_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_kafka)
  ce_add_codec_test(nats_binding_test
    SOURCES nats_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_nats)
  ce_add_test(extensions_test
    SOURCES extensions_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(typed_payload_test
    SOURCES typed_payload_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(roundtrip_test
    SOURCES roundtrip_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(errors_test
    SOURCES errors_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(conformance_test
    SOURCES conformance_test.cpp
    LIBS ce::codec_nlohmann)
  # The fixtures are read from the source tree, not copied into the build tree:
  # a stale copy would let the suite pass against bytes the repository no longer
  # holds.
  target_compile_definitions(conformance_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/conformance\"")
  ce_add_test(interop_test
    SOURCES interop_test.cpp
    LIBS ce::codec_nlohmann)
  target_compile_definitions(interop_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\"")
endif()

# Marking the dependencies SYSTEM must scope the suppression to THEIR headers and
# not to ours. That is easy to get wrong later -- someone marks our own include
# directory SYSTEM to silence something -- and the failure is invisible: the build
# stays green while our own warnings quietly stop being errors.
#
# So build a translation unit that includes CTRE and also contains a narrowing
# conversion of our own, through the REAL targets and the REAL warning set, and
# assert that building it FAILS. try_compile cannot do this job: it builds a
# separate project that cannot see ce::core or ce::warnings, so it would test a
# hand-written command line rather than the configuration we actually ship.
add_executable(warning_scope_probe EXCLUDE_FROM_ALL warning_scope_probe.cpp)
target_link_libraries(warning_scope_probe PRIVATE ce::core ce::warnings)

add_test(NAME warning-scope-probe-must-not-compile
  COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target warning_scope_probe)
set_tests_properties(warning-scope-probe-must-not-compile PROPERTIES
  WILL_FAIL TRUE
  LABELS "guard")

# SWR-CORE-0027 says an invalid attribute literal rejects the translation unit
# rather than producing a value. Nothing inside a running program can witness
# that, because a program containing the offending line cannot be built. Same
# shape as the probe above, and the same reason try_compile will not do: the
# literal has to meet the real ce::core.
add_executable(attribute_literal_probe EXCLUDE_FROM_ALL attribute_literal_probe.cpp)
target_link_libraries(attribute_literal_probe PRIVATE ce::core ce::warnings)

add_test(NAME attribute-literal-probe-must-not-compile
  COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target attribute_literal_probe)
set_tests_properties(attribute-literal-probe-must-not-compile PROPERTIES
  WILL_FAIL TRUE
  LABELS "guard")

# --- build-level checks named by requirements ------------------------------
#
# These are targets rather than ut suites, because what they assert is a
# property of the BUILD: that the whole suite runs clean under the sanitizers,
# and that line coverage reaches the floor. check_references.py resolves a
# verified_by that names this file by looking for the target name in it.

# SWR-SEC-0002: the complete suite under ASan and UBSan, failing on any report.
# The asan preset supplies -fsanitize=address,undefined together with
# -fno-sanitize-recover=all, so a report aborts rather than being counted and
# ignored. Running ctest through a target keeps the invocation in one place.
add_custom_target(asan-ubsan-suite
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running the whole suite under AddressSanitizer and UndefinedBehaviorSanitizer")

# SWR-SEC-0007: line coverage for core, format and binding, with the floor
# enforced rather than reported. gcovr exits non-zero below it, so the target
# fails the build instead of printing a number nobody reads.
set(CE_COVERAGE_FLOOR 90 CACHE STRING "Minimum line coverage percent for the headers")
# gcovr defaults to `gcov`, which on macOS is Apple's and cannot read GCC's
# .gcda. It reports every header as 0 percent rather than failing, so the
# executable is named explicitly and defaults to the one beside the compiler.
set(CE_GCOV_EXECUTABLE "gcov" CACHE STRING "The gcov matching CMAKE_CXX_COMPILER")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/coverage)
add_custom_target(coverage-report
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  COMMAND gcovr --root ${CMAKE_SOURCE_DIR}
          --gcov-executable ${CE_GCOV_EXECUTABLE}
          --filter ${CMAKE_SOURCE_DIR}/include/cloudevents/
          --exclude ${CMAKE_SOURCE_DIR}/include/cloudevents/detail/expected_polyfill.hpp
          --print-summary
          --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
          --xml ${CMAKE_BINARY_DIR}/coverage/coverage.xml
          --fail-under-line ${CE_COVERAGE_FLOOR}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Measuring line coverage for the SDK headers, floor ${CE_COVERAGE_FLOOR} percent")
