# libFuzzer targets for every routine that parses untrusted input.
#
# The preset compiles the whole tree with -fsanitize=fuzzer-no-link so coverage
# instrumentation reaches the headers; each target links the driver itself.
#
# Not part of the default build: CE_BUILD_FUZZERS is OFF unless a preset sets it.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "ce: fuzz targets need Clang for libFuzzer; skipping")
  return()
endif()

function(ce_add_fuzzer name)
  cmake_parse_arguments(ARG "" "CORPUS" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ce::core ce::warnings ${ARG_LIBS})
  target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)

  # A short run in CTest, so a crash surfaces on every build rather than only in
  # a dedicated fuzzing job. The ten-minute runs SWR-SEC-0001 requires are a CI
  # concern; this keeps the corpus honest day to day.
  #
  # libFuzzer writes every input it discovers into the FIRST corpus directory on
  # the command line, so a scratch directory goes there and the checked-in seeds
  # follow it read-only. Naming the seed directory alone buries 21 curated seeds
  # under thousands of unreviewed blobs.
  if(ARG_CORPUS)
    set(found ${CMAKE_CURRENT_BINARY_DIR}/${name}-found)
    file(MAKE_DIRECTORY ${found})
    add_test(NAME ${name}-smoke
      COMMAND ${name} ${found} ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CORPUS}
              -runs=2000 -timeout=10 -artifact_prefix=${found}/)
    set_tests_properties(${name}-smoke PROPERTIES LABELS "fuzz")
  endif()
endfunction()

ce_add_fuzzer(fuzz_timestamp SOURCES fuzz_timestamp.cpp CORPUS corpus/timestamp)
ce_add_fuzzer(fuzz_base64 SOURCES fuzz_base64.cpp CORPUS corpus/base64)

if(CE_CODEC_NLOHMANN)
  ce_add_fuzzer(fuzz_json_decode SOURCES fuzz_json_decode.cpp
    LIBS ce::codec_nlohmann CORPUS corpus/json)
  ce_add_fuzzer(fuzz_http_message SOURCES fuzz_http_message.cpp
    LIBS ce::codec_nlohmann ce::binding_http CORPUS corpus/http)
  ce_add_fuzzer(fuzz_kafka_message SOURCES fuzz_kafka_message.cpp
    LIBS ce::codec_nlohmann ce::binding_kafka CORPUS corpus/kafka)
  ce_add_fuzzer(fuzz_nats_payload SOURCES fuzz_nats_payload.cpp
    LIBS ce::codec_nlohmann ce::binding_nats CORPUS corpus/nats)
  ce_add_fuzzer(fuzz_typed SOURCES fuzz_typed.cpp
    LIBS ce::codec_nlohmann CORPUS corpus/typed)
  target_include_directories(fuzz_json_decode PRIVATE ${CMAKE_SOURCE_DIR})
endif()

add_subdirectory(v1)
add_subdirectory(v2)
