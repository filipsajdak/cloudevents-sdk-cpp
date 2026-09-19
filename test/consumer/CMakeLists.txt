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
if(TARGET ce::codec_nlohmann)
  add_executable(consumer_codec codec_main.cpp)
  target_link_libraries(consumer_codec PRIVATE ce::codec_nlohmann)
  target_compile_features(consumer_codec PRIVATE cxx_std_20)
endif()

enable_testing()
add_test(NAME consumer-find-package COMMAND consumer_find_package)
add_test(NAME consumer-fno-exceptions COMMAND consumer_fno_exceptions)
if(TARGET consumer_codec)
  add_test(NAME consumer-codec-link COMMAND consumer_codec)
endif()
