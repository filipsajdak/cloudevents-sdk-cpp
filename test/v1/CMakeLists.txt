# The v0.3.0 suites, run against ce::v1 (SWR-BUILD-0011, ADR-0009).
#
# They are the suites v0.3.0 was released against, spelled ce::v1:: instead of
# ce::, so a v1 declaration or behaviour that changes fails here. Each target is
# prefixed v1_ and labelled "v1". They stay out of the clang-tidy gate: ce::v1 is
# frozen at the v0.3.0 shape, and reshaping it to satisfy a later check is the
# change this generation exists to refuse (D-TIDY-4).

set(CE_V1_CODEC_INCLUDES "")
set(CE_V1_CODEC_VISITS "")
foreach(codec IN LISTS CE_CODECS)
  string(APPEND CE_V1_CODEC_INCLUDES "#include <cloudevents/codec/${codec}.hpp>\n")
  string(APPEND CE_V1_CODEC_VISITS
         "  visit.template operator()<::ce::v1::codec::${codec}_codec>(std::string_view{\"${codec}_codec\"});\n")
endforeach()
set(CE_CODEC_INCLUDES "${CE_V1_CODEC_INCLUDES}")
set(CE_CODEC_VISITS "${CE_V1_CODEC_VISITS}")
configure_file(codecs_under_test.hpp.in
               "${CMAKE_CURRENT_BINARY_DIR}/gen/codecs_under_test.hpp" @ONLY)

function(ce_add_v1_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(v1_${name} ${ARG_SOURCES})
  target_link_libraries(v1_${name} PRIVATE ce::core ce::warnings Boost::ut ${ARG_LIBS})
  target_include_directories(v1_${name} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME v1_${name} COMMAND v1_${name})
  set_tests_properties(v1_${name} PROPERTIES LABELS "v1")
endfunction()

function(ce_add_v1_codec_test name)
  ce_add_v1_test(${name} ${ARGN})
  ce_codec_targets(codec_targets)
  target_link_libraries(v1_${name} PRIVATE ${codec_targets})
endfunction()

ce_add_v1_test(generation_test SOURCES v1_generation_test.cpp)
ce_add_v1_test(config_test SOURCES config_test.cpp)
ce_add_v1_test(core_test SOURCES core_test.cpp)
ce_add_v1_test(describe_parity_test SOURCES describe_parity_test.cpp)
ce_add_v1_test(base64_test SOURCES base64_test.cpp)
ce_add_v1_test(binding_core_test SOURCES binding_core_test.cpp)

if(CE_CODEC_NLOHMANN)
  ce_add_v1_codec_test(json_codec_test
    SOURCES json_codec_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  target_compile_definitions(v1_json_codec_test
    PRIVATE "CE_INCLUDE_DIR=\"${PROJECT_SOURCE_DIR}/include\"")
  ce_add_v1_codec_test(json_format_test SOURCES json_format_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v1_codec_test(http_binding_test SOURCES http_binding_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v1_codec_test(kafka_binding_test
    SOURCES kafka_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_kafka)
  ce_add_v1_codec_test(nats_binding_test
    SOURCES nats_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_nats)
  ce_add_v1_test(extensions_test SOURCES extensions_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v1_test(typed_payload_test
    SOURCES typed_payload_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  ce_add_v1_test(roundtrip_test SOURCES roundtrip_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v1_test(errors_test SOURCES errors_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v1_test(conformance_test SOURCES conformance_test.cpp LIBS ce::codec_nlohmann)
  target_compile_definitions(v1_conformance_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/conformance\"")
  ce_add_v1_test(interop_test SOURCES interop_test.cpp LIBS ce::codec_nlohmann)
  target_compile_definitions(v1_interop_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../fixtures\"")
endif()

# The build-level check SWR-BUILD-0011 names: every v1 suite, and nothing else.
add_custom_target(ce-v1-suites
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L v1
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running the v0.3.0 suites against ce::v1")
