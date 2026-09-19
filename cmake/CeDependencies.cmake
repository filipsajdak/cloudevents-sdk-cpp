# Dependency acquisition.
#
# find_package is preferred so a distribution or a superproject that already
# supplies a dependency wins; FetchContent is the fallback, pinned by commit hash
# rather than by tag so a retagged upstream cannot change what we build
# (SPEC section 7, M0).

include(FetchContent)

# The core's only third-party dependency.
find_package(ctre QUIET)
if(NOT ctre_FOUND)
  FetchContent_Declare(ctre
    GIT_REPOSITORY https://github.com/hanickadot/compile-time-regular-expressions.git
    GIT_TAG 78c5c3ef0e562f2ef0fea9ee3de743cdcf99e6f2  # v3.9.0
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(ctre)
endif()


# Pinned to the version the first consuming platform vendors.
if(CE_DEFAULT_CODEC)
  find_package(nlohmann_json 3.12.0 QUIET)
  if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b  # v3.12.0
      GIT_SHALLOW FALSE)
    # Defaults OFF for a subproject, which would leave the installed package
    # unable to satisfy its own find_dependency(nlohmann_json).
    set(JSON_Install ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

if(CE_BUILD_TESTING)
  find_package(ut QUIET)
  if(NOT ut_FOUND)
    FetchContent_Declare(ut
      GIT_REPOSITORY https://github.com/boost-ext/ut.git
      GIT_TAG f923e6fe4b7542d75e0c4ee54ad0af6a5382a87c  # v2.3.1
      GIT_SHALLOW FALSE)
    set(BOOST_UT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_DISABLE_MODULE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ut)

    # Mach-O has no constructor priorities, so GCC on macOS rejects ut's
    # __attribute__((constructor(101))). Patched at configure time so a failed
    # match is visible here rather than as the original compile error.
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      set(CE_UT_HEADER "${ut_SOURCE_DIR}/include/boost/ut.hpp")
      file(READ "${CE_UT_HEADER}" CE_UT_SOURCE)
      string(FIND "${CE_UT_SOURCE}" "__attribute__((constructor(101)))" CE_UT_MATCH)
      if(NOT CE_UT_MATCH EQUAL -1)
        string(REPLACE
          "__attribute__((constructor(101)))"
          "__attribute__((constructor))"
          CE_UT_SOURCE "${CE_UT_SOURCE}")
        file(WRITE "${CE_UT_HEADER}" "${CE_UT_SOURCE}")
        message(STATUS "ce: dropped the ut constructor priority for GCC on macOS "
                       "(Mach-O has no constructor priorities)")
      endif()
    endif()
  endif()
endif()

# Dependency headers are SYSTEM headers: CTRE 3.9.0 does not compile under any
# current Clang with our warning set. Scoping the suppression to headers we do not
# maintain keeps our own diagnostics fatal, which warning_scope_probe.cpp asserts.
foreach(dependency IN ITEMS ctre nlohmann_json)
  if(TARGET ${dependency})
    set_target_properties(${dependency} PROPERTIES SYSTEM TRUE)
  endif()
endforeach()
