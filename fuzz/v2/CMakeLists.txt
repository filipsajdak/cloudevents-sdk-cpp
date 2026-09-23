# The v0.4.0 fuzz targets, spelled against ce::v2 (SWR-BUILD-0012). They share
# the seed corpora with the v3 targets, because the wire formats are the same.

set(CE_V2_CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/../corpus)

function(ce_add_v2_fuzzer name)
  cmake_parse_arguments(ARG "" "CORPUS" "SOURCES;LIBS" ${ARGN})
  add_executable(v2_${name} ${ARG_SOURCES})
  target_link_libraries(v2_${name} PRIVATE ce::core ce::warnings ${ARG_LIBS})
  target_compile_options(v2_${name} PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(v2_${name} PRIVATE -fsanitize=fuzzer)
  set(found ${CMAKE_CURRENT_BINARY_DIR}/v2_${name}-found)
  file(MAKE_DIRECTORY ${found})
  add_test(NAME v2_${name}-smoke
    COMMAND v2_${name} ${found} ${CE_V2_CORPUS}/${ARG_CORPUS}
            -runs=2000 -timeout=10 -artifact_prefix=${found}/)
  set_tests_properties(v2_${name}-smoke PROPERTIES LABELS "fuzz;v2")
endfunction()

ce_add_v2_fuzzer(fuzz_timestamp SOURCES fuzz_timestamp.cpp CORPUS timestamp)
ce_add_v2_fuzzer(fuzz_base64 SOURCES fuzz_base64.cpp CORPUS base64)

if(CE_CODEC_NLOHMANN)
  ce_add_v2_fuzzer(fuzz_json_decode SOURCES fuzz_json_decode.cpp
    LIBS ce::codec_nlohmann CORPUS json)
  ce_add_v2_fuzzer(fuzz_http_message SOURCES fuzz_http_message.cpp
    LIBS ce::codec_nlohmann ce::binding_http CORPUS http)
  ce_add_v2_fuzzer(fuzz_kafka_message SOURCES fuzz_kafka_message.cpp
    LIBS ce::codec_nlohmann ce::binding_kafka CORPUS kafka)
  ce_add_v2_fuzzer(fuzz_nats_payload SOURCES fuzz_nats_payload.cpp
    LIBS ce::codec_nlohmann ce::binding_nats CORPUS nats)
  ce_add_v2_fuzzer(fuzz_typed SOURCES fuzz_typed.cpp
    LIBS ce::codec_nlohmann CORPUS typed)
  target_include_directories(v2_fuzz_json_decode PRIVATE ${CMAKE_SOURCE_DIR})
endif()
