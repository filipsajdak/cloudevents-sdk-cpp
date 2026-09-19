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

enable_testing()
add_test(NAME consumer-find-package COMMAND consumer_find_package)
add_test(NAME consumer-fno-exceptions COMMAND consumer_fno_exceptions)
