# The performance job's measuring binaries (ADR-0011). Built with the
# benchmark, and like it at C++23 because of Glaze, but needing neither Google
# Benchmark nor the ceiling libraries.
#
#   perf_probe             allocations and retained bytes, counted exactly
#   perf_probe_instr       the same operations for Callgrind, with no counting
#   perf_consumer_<codec>  a minimal consumer, whose stripped size is measured
#
# measure.py drives both; see docs/PERFORMANCE.md.

set(ce_bench_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")

include("${CMAKE_CURRENT_SOURCE_DIR}/malloc_wrap.cmake")
set(ce_perf_bindings ce::binding_http ce::binding_kafka ce::binding_nats)
ce_add_bench(perf_probe
  SOURCES perf_probe.cpp counting_allocator.cpp heap_counters.cpp ${CE_PERF_RAW_LAYER}
  LIBS ${ce_perf_bindings})
target_include_directories(perf_probe PRIVATE ${ce_bench_dir})
ce_perf_wrap_malloc(perf_probe)

# Instructions are counted in a binary with no allocation accounting: neither
# the counting operator new nor the malloc wrap, so Callgrind sees only the SDK
# and the codec. The counters it links stay at zero and are never read.
ce_add_bench(perf_probe_instr
  SOURCES perf_probe.cpp heap_counters.cpp raw_malloc_plain.cpp
  LIBS ${ce_perf_bindings})
target_include_directories(perf_probe_instr PRIVATE ${ce_bench_dir})

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
add_dependencies(perf_measure perf_probe perf_probe_instr)
foreach(entry IN LISTS ce_perf_consumers)
  string(REGEX REPLACE "\\|.*" "" codec "${entry}")
  string(REPLACE "." "_" target_codec "${codec}")
  add_dependencies(perf_measure perf_consumer_${target_codec})
endforeach()
