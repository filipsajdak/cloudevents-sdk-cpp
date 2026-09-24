# The performance job's measuring binaries (ADR-0011). Built with the
# benchmark, and like it at C++23 because of Glaze, but needing neither Google
# Benchmark nor the ceiling libraries.
#
#   perf_probe             instructions, allocations and retained bytes
#   perf_consumer_<codec>  a minimal consumer, whose stripped size is measured
#
# measure.py drives both; see docs/PERFORMANCE.md.

set(ce_bench_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")

# On Linux the linker wraps malloc for this binary, so RapidJSON's direct
# malloc calls are counted with everything that goes through operator new.
# Other linkers have no --wrap, and there the probe says it counts operator new
# alone.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(ce_perf_raw_layer raw_malloc_wrapped.cpp)
else()
  set(ce_perf_raw_layer raw_malloc_plain.cpp)
endif()

ce_add_bench(perf_probe SOURCES perf_probe.cpp counting_allocator.cpp ${ce_perf_raw_layer})
target_include_directories(perf_probe PRIVATE ${ce_bench_dir})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(perf_probe PRIVATE
    "LINKER:--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free")
endif()

set(ce_perf_consumers
  "nlohmann|<cloudevents/codec/nlohmann.hpp>|ce::codec::nlohmann_codec"
  "rapidjson|\"codecs/rapidjson_codec.hpp\"|ce::bench::rapidjson_codec"
  "boost.json|\"codecs/boost_json_codec.hpp\"|ce::bench::boost_json_codec"
  "glaze|\"codecs/glaze_codec.hpp\"|ce::bench::glaze_codec")
foreach(entry IN LISTS ce_perf_consumers)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 codec)
  list(GET fields 1 header)
  list(GET fields 2 type)
  string(REPLACE "." "_" target_codec "${codec}")
  set(target perf_consumer_${target_codec})
  ce_add_bench(${target} SOURCES consumer.cpp)
  target_include_directories(${target} PRIVATE ${ce_bench_dir})
  target_compile_definitions(${target} PRIVATE
    "CE_PERF_CODEC_HEADER=${header}" "CE_PERF_CODEC_TYPE=${type}")
endforeach()

# Everything measure.py runs. bench/CMakeLists.txt adds codec_bench to it when
# Google Benchmark and the ceiling libraries are present.
add_custom_target(perf_measure)
add_dependencies(perf_measure perf_probe)
foreach(entry IN LISTS ce_perf_consumers)
  string(REGEX REPLACE "\\|.*" "" codec "${entry}")
  string(REPLACE "." "_" target_codec "${codec}")
  add_dependencies(perf_measure perf_consumer_${target_codec})
endforeach()
