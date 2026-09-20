# The examples are built by CI, not by the default build: they are documentation
# that has to keep compiling, and a consumer building the SDK does not need them.
#
# Each is also registered as a test. An example that compiles but exits non-zero
# is a broken example, and that is exactly what a reader would hit.

function(ce_add_example name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(example_${name} ${ARG_SOURCES})
  target_link_libraries(example_${name} PRIVATE ce::core ce::warnings ${ARG_LIBS})
  if(CE_BUILD_TESTING)
    add_test(NAME example_${name} COMMAND example_${name})
    set_tests_properties(example_${name} PROPERTIES LABELS "example")
  endif()
endfunction()

# custom_codec deliberately links no codec target: supplying your own is the
# whole point, and nothing here may include nlohmann.
ce_add_example(custom_codec SOURCES custom_codec.cpp)

if(CE_CODEC_NLOHMANN)
  ce_add_example(produce SOURCES produce.cpp LIBS ce::codec_nlohmann)
  ce_add_example(consume SOURCES consume.cpp LIBS ce::codec_nlohmann)
  ce_add_example(described_payload
    SOURCES described_payload.cpp
    LIBS ce::codec_nlohmann)
endif()
