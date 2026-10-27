# A standalone project, deliberately NOT part of the main build.
#
# SPEC section 7 M0 makes packaging an acceptance test rather than an assumption:
# the SDK is installed, then this project finds it with find_package and compiles
# against the imported targets with no include path named by hand. Anything that
# only works from inside the source tree fails here (SWR-ADOPT-0004).

cmake_minimum_required(VERSION 3.25)
project(cloudevents_consumer LANGUAGES CXX)

find_package(cloudevents REQUIRED)

add_executable(consumer_find_package main.cpp)
target_link_libraries(consumer_find_package PRIVATE ce::core)
target_compile_features(consumer_find_package PRIVATE cxx_std_20)

# The core must be usable with exceptions disabled (SPEC section 9, D4).
# SWR-ADOPT-0003.
add_executable(consumer_fno_exceptions main.cpp)
target_link_libraries(consumer_fno_exceptions PRIVATE ce::core)
target_compile_features(consumer_fno_exceptions PRIVATE cxx_std_20)
if(NOT MSVC)
  target_compile_options(consumer_fno_exceptions PRIVATE -fno-exceptions)
endif()

# The codec target is the one that can break on install while every in-tree build
# stays green: install(EXPORT) cannot carry a link to nlohmann, so the config file
# re-attaches it. Nothing catches a mistake there unless a consumer actually links
# ce::codec_nlohmann and includes a header that needs nlohmann's own headers.
# The component, not the target: the target exists whenever the SDK was built
# with the codec, but it is only usable once nlohmann has actually been found.
if(cloudevents_codec_nlohmann_FOUND)
  add_executable(consumer_codec codec_main.cpp)
  target_link_libraries(consumer_codec PRIVATE ce::codec_nlohmann)
  target_compile_features(consumer_codec PRIVATE cxx_std_20)

  # The two above prove that headers are found. This one runs the library: an
  # event goes out through the HTTP binding in both content modes and comes
  # back equal, which executes CTRE, the codec and the format layer.
  add_executable(consumer_roundtrip roundtrip_main.cpp)
  target_link_libraries(consumer_roundtrip PRIVATE ce::codec_nlohmann ce::binding_http)
  target_compile_features(consumer_roundtrip PRIVATE cxx_std_20)
endif()

# The same run through the RapidJSON codec, whose dependency is found by header
# and whose include directory a package manager has to put where find_path looks.
if(cloudevents_codec_rapidjson_FOUND)
  add_executable(consumer_roundtrip_rapidjson roundtrip_rapidjson_main.cpp)
  target_link_libraries(consumer_roundtrip_rapidjson
    PRIVATE ce::codec_rapidjson ce::binding_http)
  target_compile_features(consumer_roundtrip_rapidjson PRIVATE cxx_std_20)
endif()

# SWR-ADOPT-0001: the targets exist in the installed package, with the
# dependency direction the SPEC fixes. A missing or misnamed target fails at
# configure time, which is earlier and clearer than a link error.
foreach(expected ce::core ce::format_json ce::binding_http)
  if(NOT TARGET ${expected})
    message(FATAL_ERROR "consumer-target-graph: the installed package has no ${expected}")
  endif()
endforeach()

add_executable(consumer_target_graph target_graph_main.cpp)
target_link_libraries(consumer_target_graph
  PRIVATE ce::core ce::format_json ce::binding_http)
target_compile_features(consumer_target_graph PRIVATE cxx_std_20)

# SWR-PROTO-0014: the Protobuf format is a target of its own that links ce::core and nothing
# else. It has no dependency, so it is found whenever the package is, with no component.
if(NOT TARGET ce::format_protobuf)
  message(FATAL_ERROR "consumer-target-graph-protobuf: the installed package has no ce::format_protobuf")
endif()
get_target_property(protobuf_links ce::format_protobuf INTERFACE_LINK_LIBRARIES)
if(NOT "${protobuf_links}" STREQUAL "ce::core")
  message(FATAL_ERROR
    "consumer-target-graph-protobuf: ce::format_protobuf links '${protobuf_links}', not ce::core only")
endif()

add_executable(consumer_target_graph_protobuf protobuf_graph_main.cpp)
target_link_libraries(consumer_target_graph_protobuf PRIVATE ce::format_protobuf)
target_compile_features(consumer_target_graph_protobuf PRIVATE cxx_std_20)

# The format with exceptions off, as the core is checked (SWR-ADOPT-0003): it runs the library, so
# an event goes out and comes back equal through nothing but the installed target.
add_executable(consumer_protobuf_roundtrip protobuf_main.cpp)
target_link_libraries(consumer_protobuf_roundtrip PRIVATE ce::format_protobuf)
target_compile_features(consumer_protobuf_roundtrip PRIVATE cxx_std_20)
if(NOT MSVC)
  target_compile_options(consumer_protobuf_roundtrip PRIVATE -fno-exceptions)
endif()

# SWR-ADOPT-0002: ce::core alone, with nothing else on the link line. If core
# ever grows a dependency, this is where it surfaces.
add_executable(consumer_no_nlohmann no_nlohmann_main.cpp)
target_link_libraries(consumer_no_nlohmann PRIVATE ce::core)
target_compile_features(consumer_no_nlohmann PRIVATE cxx_std_20)

# SWR-MQTT-0017: the MQTT binding is a target of the installed package, depends on ce::core and
# names no client library, so it links with nothing else on the line.
if(NOT TARGET ce::binding_mqtt)
  message(FATAL_ERROR "consumer-mqtt-target: the installed package has no ce::binding_mqtt")
endif()
add_executable(consumer_mqtt_target mqtt_main.cpp)
target_link_libraries(consumer_mqtt_target PRIVATE ce::binding_mqtt)
target_compile_features(consumer_mqtt_target PRIVATE cxx_std_20)

enable_testing()
add_test(NAME consumer-find-package COMMAND consumer_find_package)
add_test(NAME consumer-target-graph COMMAND consumer_target_graph)
add_test(NAME consumer-mqtt-target COMMAND consumer_mqtt_target)
add_test(NAME consumer-no-nlohmann COMMAND consumer_no_nlohmann)
add_test(NAME consumer-target-graph-protobuf COMMAND consumer_target_graph_protobuf)
add_test(NAME consumer-protobuf-roundtrip COMMAND consumer_protobuf_roundtrip)
add_test(NAME consumer-fno-exceptions COMMAND consumer_fno_exceptions)
if(TARGET consumer_codec)
  add_test(NAME consumer-codec-link COMMAND consumer_codec)
endif()
if(TARGET consumer_roundtrip)
  add_test(NAME consumer-roundtrip COMMAND consumer_roundtrip)
endif()
if(TARGET consumer_roundtrip_rapidjson)
  add_test(NAME consumer-roundtrip-rapidjson COMMAND consumer_roundtrip_rapidjson)
endif()
