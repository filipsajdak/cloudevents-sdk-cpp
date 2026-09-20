cmake_minimum_required(VERSION 3.25)

project(cloudevents
  VERSION 0.1.0
  DESCRIPTION "CloudEvents v1.0.2 SDK for C++"
  HOMEPAGE_URL "https://github.com/filipsajdak/cloudevents-sdk-cpp"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakeDependentOption)

set(CE_IS_TOP_LEVEL OFF)
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CE_IS_TOP_LEVEL ON)
endif()

option(CE_DEFAULT_CODEC "Build the nlohmann/json codec target" ON)
cmake_dependent_option(CE_BUILD_TESTING "Build the test suite" ON "CE_IS_TOP_LEVEL" OFF)
option(CE_BUILD_FUZZERS "Build the libFuzzer targets" OFF)
option(CE_BUILD_EXAMPLES "Build the examples" OFF)
# Kept OFF by default and never made dependent on the compiler: module support
# across the toolchains SPEC section 8 names is uneven, and a module target that
# switched itself on would break the floor configuration (SWR-BUILD-0010).
option(CE_BUILD_MODULE "Build the optional cloudevents.cppm module interface" OFF)
# The JSON codec comparison. Off by default: it needs four third-party JSON
# libraries that are not dependencies of the SDK.
option(CE_BUILD_BENCHMARKS "Build the JSON codec benchmark" OFF)
option(CE_WERROR "Treat warnings as errors" ${CE_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CeWarnings)
include(CeDependencies)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
# Four header-only interface libraries per SPEC section 4. The dependency
# direction runs strictly downward: bindings and formats may see the core, the
# core never sees them. CMake enforces it here, so a stray include in the core
# fails to configure rather than surviving on convention (SWR-ADOPT-0001).

add_library(ce_core INTERFACE)
add_library(ce::core ALIAS ce_core)
# EXPORT_NAME is not cosmetic. install(EXPORT ... NAMESPACE ce::) prepends the
# namespace to the *raw* target name, so without this the installed package offers
# ce::ce_core and every consumer written against ce::core fails to configure. The
# in-tree ALIAS above hides the difference, which is why only an actual consumer
# build catches it.
set_target_properties(ce_core PROPERTIES EXPORT_NAME core)
target_include_directories(ce_core INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# The floor, and only the floor. Note that CMake appends this target's -std AFTER
# anything in CMAKE_CXX_FLAGS, so a preset cannot raise the dialect by putting
# -std=... in the flags: the last one wins and it is this one. Presets raise the
# dialect with CMAKE_CXX_STANDARD instead (docs/DECISIONS.md D-CONFIG-2).
target_compile_features(ce_core INTERFACE cxx_std_20)
target_link_libraries(ce_core INTERFACE ctre::ctre)

add_library(ce_format_json INTERFACE)
add_library(ce::format_json ALIAS ce_format_json)
set_target_properties(ce_format_json PROPERTIES EXPORT_NAME format_json)
target_link_libraries(ce_format_json INTERFACE ce_core)

add_library(ce_binding_http INTERFACE)
add_library(ce::binding_http ALIAS ce_binding_http)
set_target_properties(ce_binding_http PROPERTIES EXPORT_NAME binding_http)
target_link_libraries(ce_binding_http INTERFACE ce_core)

# The only target permitted to see nlohmann. With CE_DEFAULT_CODEC=OFF it does not
# exist at all, which is what makes SWR-ADOPT-0002 and SWR-JSON-0030 testable
# rather than aspirational.
if(CE_DEFAULT_CODEC)
  add_library(ce_codec_nlohmann INTERFACE)
  add_library(ce::codec_nlohmann ALIAS ce_codec_nlohmann)
  set_target_properties(ce_codec_nlohmann PROPERTIES EXPORT_NAME codec_nlohmann)
  # nlohmann is linked for the BUILD only. install(EXPORT) refuses a target that
  # links something outside the export set, and FetchContent makes nlohmann a real
  # target in our build tree rather than an imported one. Consumers get the link
  # back from cloudeventsConfig.cmake, which find_dependency()s nlohmann and then
  # re-attaches it.
  #
  # This never reproduced locally: Homebrew supplies nlohmann, so find_package wins
  # and produces an IMPORTED target, which install(EXPORT) accepts. Only a machine
  # without nlohmann installed takes the FetchContent path and sees the error.
  target_link_libraries(ce_codec_nlohmann INTERFACE
    ce_format_json
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
endif()

# ---------------------------------------------------------------------------
# Tests, fuzzers, examples
# ---------------------------------------------------------------------------
if(CE_BUILD_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()

if(CE_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()

if(CE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(CE_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()

if(CE_BUILD_MODULE)
  if(CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "ce: CE_BUILD_MODULE needs CMake 3.28 or newer for C++ modules")
  endif()
  add_library(ce_module)
  add_library(ce::module ALIAS ce_module)
  set_target_properties(ce_module PROPERTIES EXPORT_NAME module)
  target_sources(ce_module
    PUBLIC FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES include/cloudevents/cloudevents.cppm)
  target_link_libraries(ce_module PUBLIC ce_core)
  target_compile_features(ce_module PUBLIC cxx_std_20)

  # A module that compiles but exports nothing usable would pass unnoticed, so
  # the optional build also consumes it.
  add_executable(module_consumer test/module_consumer.cpp)
  target_link_libraries(module_consumer PRIVATE ce_module ce::warnings)
  # Without this the consumer is compiled as an ordinary translation unit and
  # `import` is a syntax error, which is a confusing way to learn that the
  # module was never reached.
  set_target_properties(module_consumer PROPERTIES CXX_SCAN_FOR_MODULES ON)
  if(CE_BUILD_TESTING)
    add_test(NAME module_consumer COMMAND module_consumer)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

set(CE_INSTALL_TARGETS ce_core ce_format_json ce_binding_http)
if(CE_DEFAULT_CODEC)
  list(APPEND CE_INSTALL_TARGETS ce_codec_nlohmann)
endif()

install(TARGETS ${CE_INSTALL_TARGETS} EXPORT cloudeventsTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT cloudeventsTargets
  FILE cloudeventsTargets.cmake
  NAMESPACE ce::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cloudevents)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/cloudeventsConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cloudeventsConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cloudevents)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cloudeventsConfigVersion.cmake"
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/cloudeventsConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/cloudeventsConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cloudevents)
