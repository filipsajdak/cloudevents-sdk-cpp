# How a binary counts direct malloc calls: included by bench/perf for the probe
# and by test/ for the suite that checks the counting, so both link the same
# layer the same way.
#
# On Linux the linker wraps the C allocation functions for the binary, so
# RapidJSON's direct malloc calls are counted with everything that goes through
# operator new. Other linkers have no --wrap, and there the probe says it
# counts operator new alone.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CE_PERF_COUNTS_MALLOC ON)
  set(CE_PERF_RAW_LAYER "${CMAKE_CURRENT_LIST_DIR}/raw_malloc_wrapped.cpp")
else()
  set(CE_PERF_COUNTS_MALLOC OFF)
  set(CE_PERF_RAW_LAYER "${CMAKE_CURRENT_LIST_DIR}/raw_malloc_plain.cpp")
endif()

function(ce_perf_wrap_malloc target)
  if(CE_PERF_COUNTS_MALLOC)
    target_link_options(${target} PRIVATE
      "LINKER:--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"
      "LINKER:--wrap=aligned_alloc,--wrap=posix_memalign,--wrap=memalign")
  endif()
endfunction()
