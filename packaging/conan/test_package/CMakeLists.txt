# The consumer is test/consumer, not a copy of it: what proves the CMake install
# proves the Conan package too. It calls find_package(cloudevents) itself.
cmake_minimum_required(VERSION 3.25)
project(cloudevents_test_package LANGUAGES CXX)

enable_testing()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../test/consumer" consumer)
