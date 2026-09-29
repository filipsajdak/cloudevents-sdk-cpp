# The v0.5.0 suites, run against ce::v3 (SWR-BUILD-0013, ADR-0012).
#
# They are the suites v0.5.0 was released against, spelled ce::v3:: instead of
# ce::, so a v3 declaration or behaviour that changes fails here. Each target is
# prefixed v3_ and labelled "v3". They stay out of the clang-tidy gate: ce::v3 is
# frozen at the v0.5.0 shape, and reshaping it to satisfy a later check is the
# change this generation exists to refuse (D-TIDY-5).

set(CE_V3_CODEC_INCLUDES "")
set(CE_V3_CODEC_VISITS "")
foreach(codec IN LISTS CE_CODECS)
  string(APPEND CE_V3_CODEC_INCLUDES "#include <cloudevents/codec/${codec}.hpp>\n")
  string(APPEND CE_V3_CODEC_VISITS
         "  visit.template operator()<::ce::v3::codec::${codec}_codec>(std::string_view{\"${codec}_codec\"});\n")
endforeach()
set(CE_CODEC_INCLUDES "${CE_V3_CODEC_INCLUDES}")
set(CE_CODEC_VISITS "${CE_V3_CODEC_VISITS}")
configure_file(codecs_under_test.hpp.in
               "${CMAKE_CURRENT_BINARY_DIR}/gen/codecs_under_test.hpp" @ONLY)

function(ce_add_v3_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(v3_${name} ${ARG_SOURCES})
  target_link_libraries(v3_${name} PRIVATE ce::core ce::warnings Boost::ut ${ARG_LIBS})
  target_include_directories(v3_${name} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME v3_${name} COMMAND v3_${name})
  set_tests_properties(v3_${name} PROPERTIES LABELS "v3")
endfunction()

function(ce_add_v3_codec_test name)
  ce_add_v3_test(${name} ${ARGN})
  ce_codec_targets(codec_targets)
  target_link_libraries(v3_${name} PRIVATE ${codec_targets})
endfunction()

ce_add_v3_test(generation_test SOURCES v3_generation_test.cpp)
ce_add_v3_test(core_test SOURCES core_test.cpp)
ce_add_v3_test(static_error_test SOURCES static_error_test.cpp)
ce_add_v3_test(attribute_types_test SOURCES attribute_types_test.cpp)
ce_add_v3_test(event_builder_test SOURCES event_builder_test.cpp)
ce_add_v3_test(describe_parity_test SOURCES describe_parity_test.cpp)
ce_add_v3_test(base64_test SOURCES base64_test.cpp)
ce_add_v3_test(binding_core_test SOURCES binding_core_test.cpp)
ce_add_v3_test(message_headers_test SOURCES message_headers_test.cpp)
ce_add_v3_test(http_encoding_test SOURCES http_encoding_test.cpp)
ce_add_v3_test(timestamp_test SOURCES timestamp_test.cpp)

if(CE_CODEC_NLOHMANN)
  ce_add_v3_codec_test(json_codec_test
    SOURCES json_codec_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  target_compile_definitions(v3_json_codec_test
    PRIVATE "CE_INCLUDE_DIR=\"${PROJECT_SOURCE_DIR}/include\"")
  ce_add_v3_codec_test(json_format_test SOURCES json_format_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v3_codec_test(http_binding_test SOURCES http_binding_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v3_codec_test(kafka_binding_test
    SOURCES kafka_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_kafka)
  ce_add_v3_codec_test(nats_binding_test
    SOURCES nats_binding_test.cpp
    LIBS ce::codec_nlohmann ce::binding_nats)
  ce_add_v3_test(extensions_test SOURCES extensions_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v3_test(typed_payload_test
    SOURCES typed_payload_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann)
  find_package(Threads REQUIRED)
  ce_add_v3_test(json_document_test
    SOURCES json_document_test.cpp no_nlohmann_probe.cpp
    LIBS ce::codec_nlohmann Threads::Threads)
  ce_add_v3_test(roundtrip_test SOURCES roundtrip_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v3_test(errors_test SOURCES errors_test.cpp LIBS ce::codec_nlohmann)
  ce_add_v3_test(conformance_test SOURCES conformance_test.cpp LIBS ce::codec_nlohmann)
  target_compile_definitions(v3_conformance_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/conformance\"")
  ce_add_v3_test(interop_test SOURCES interop_test.cpp LIBS ce::codec_nlohmann)
  target_compile_definitions(v3_interop_test
    PRIVATE "CE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../fixtures\"")
endif()

# The build-level check SWR-BUILD-0013 names: every v3 suite, and nothing else.
add_custom_target(ce-v3-suites
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L v3
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running the v0.5.0 suites against ce::v3")
