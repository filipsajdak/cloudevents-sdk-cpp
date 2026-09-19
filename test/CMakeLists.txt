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
