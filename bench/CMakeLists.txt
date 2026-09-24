# The JSON codec comparison. Off by default: it needs four third-party JSON
# libraries plus Google Benchmark, and none of them is a dependency of the SDK.
#
#   cmake -S . -B build -DCE_BUILD_BENCHMARKS=ON
#   cmake --build build --target codec_check && ./build/bench/codec_check
#   cmake --build build --target codec_bench && ./build/bench/codec_bench
#
# C++23, not the C++20 floor, because Glaze does not compile at C++20 - it uses
# std::unreachable, std::byteswap and std::is_scoped_enum_v. That is a finding
# about Glaze rather than a requirement of the SDK, and it is why the benchmark
# has its own standard setting.

# A benchmark built without optimisation reports numbers 20 times slower than
# the truth and says nothing about the libraries. Refusing is better than
# printing that, because a table of numbers looks equally authoritative either
# way.
string(TOUPPER "${CMAKE_BUILD_TYPE}" ce_build_type)
if(NOT ce_build_type MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$"
   AND NOT CMAKE_CXX_FLAGS MATCHES "-O[123s]")
  message(FATAL_ERROR
    "ce: the benchmark needs an optimised build. Measured on this machine, the "
    "same decode is 6.4 us at -O2 and 112 us at -O0. Configure with "
    "-DCMAKE_BUILD_TYPE=Release.")
endif()

find_package(RapidJSON QUIET)
find_package(Boost QUIET COMPONENTS json)
find_package(glaze QUIET)
find_package(benchmark QUIET)
find_package(simdjson QUIET)

find_path(CE_RAPIDJSON_INCLUDE rapidjson/document.h)
find_path(CE_GLAZE_INCLUDE glaze/glaze.hpp)
find_path(CE_BOOST_JSON_INCLUDE boost/json/src.hpp)
find_path(CE_YYJSON_INCLUDE yyjson.h)
find_library(CE_YYJSON_LIB yyjson)

if(NOT CE_RAPIDJSON_INCLUDE OR NOT CE_GLAZE_INCLUDE OR NOT CE_BOOST_JSON_INCLUDE)
  message(STATUS "ce: benchmark needs rapidjson, glaze and Boost.JSON headers; skipping")
  return()
endif()

# Boost.JSON is compiled here rather than linked. A prebuilt libboost_json does
# not carry every template instantiation this codec reaches, and a missing one
# is a link error rather than a diagnostic. Compiling it also means every codec
# in the comparison was built by the same compiler with the same flags.
add_library(ce_bench_boost_json STATIC boost_json_src.cpp)
target_include_directories(ce_bench_boost_json SYSTEM PUBLIC ${CE_BOOST_JSON_INCLUDE})
target_compile_features(ce_bench_boost_json PUBLIC cxx_std_23)

function(ce_add_bench name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ce::core ce::format_json ce::codec_nlohmann
                                        ce_bench_boost_json ${ARG_LIBS})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(${name} SYSTEM PRIVATE ${CE_RAPIDJSON_INCLUDE} ${CE_GLAZE_INCLUDE})
  # C++23 for Glaze, and NOT ce::warnings: these are third-party headers being
  # exercised, and their warnings are not this project's to fix.
  target_compile_features(${name} PRIVATE cxx_std_23)
endfunction()

# A codec that disagrees with the others measures nothing, so this runs first
# and the benchmark is only meaningful once it passes.
ce_add_bench(codec_check SOURCES codec_check.cpp)
if(CE_BUILD_TESTING)
  add_test(NAME codec_check COMMAND codec_check)
  set_tests_properties(codec_check PROPERTIES LABELS "bench")
endif()

# The performance job's probe needs the codecs and nothing else, so it is added
# before the checks for Google Benchmark and the ceiling libraries.
add_subdirectory(perf)

if(NOT benchmark_FOUND)
  message(STATUS "ce: Google Benchmark not found; building codec_check only")
  return()
endif()
if(NOT CE_YYJSON_INCLUDE OR NOT CE_YYJSON_LIB OR NOT simdjson_FOUND)
  message(STATUS "ce: simdjson or yyjson missing; the ceiling measurements are skipped")
  return()
endif()

ce_add_bench(codec_bench
  SOURCES codec_bench.cpp
  LIBS benchmark::benchmark simdjson::simdjson ${CE_YYJSON_LIB})
target_include_directories(codec_bench SYSTEM PRIVATE ${CE_YYJSON_INCLUDE})
add_dependencies(perf_measure codec_bench)
