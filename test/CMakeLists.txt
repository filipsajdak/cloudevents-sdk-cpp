# One executable per component, each registered with CTest (SPEC section 6).
#
# ce_add_test keeps the per-suite boilerplate in one place so adding a suite is a
# single line and every suite gets the same warning set and the same ut linkage.

function(ce_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ce::core ce::warnings Boost::ut ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ce_add_test(config_test SOURCES config_test.cpp)
ce_add_test(core_test SOURCES core_test.cpp)
ce_add_test(build_test SOURCES build_test.cpp)
ce_add_test(describe_parity_test SOURCES describe_parity_test.cpp)
ce_add_test(base64_test SOURCES base64_test.cpp)

# These two name nlohmann_codec directly, and with CE_DEFAULT_CODEC=OFF the
# nlohmann target does not exist. Registering them unconditionally would make the
# no-default-codec preset fail to configure, which is the very build that proves
# SWR-JSON-0030. base64_test above has no codec dependency, so it stays.
if(CE_DEFAULT_CODEC)
  ce_add_test(json_codec_test
    SOURCES json_codec_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(json_format_test
    SOURCES json_format_test.cpp
    LIBS ce::codec_nlohmann)
  ce_add_test(http_binding_test
    SOURCES http_binding_test.cpp
    LIBS ce::codec_nlohmann)
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
