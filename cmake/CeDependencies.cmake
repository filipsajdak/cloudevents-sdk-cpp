# Dependency acquisition.
#
# find_package is preferred so a distribution or a superproject that already
# supplies a dependency wins; FetchContent is the fallback, pinned by commit hash
# rather than by tag so a retagged upstream cannot change what we build
# (SPEC section 7, M0).

include(FetchContent)

# CTRE is the core's only third-party dependency. SPEC forbids std::regex, and
# every pattern in the SDK (RFC 3339 timestamps, attribute names, content types)
# is compile-time constant, so a compile-time engine costs nothing at runtime.
find_package(ctre QUIET)
if(NOT ctre_FOUND)
  FetchContent_Declare(ctre
    GIT_REPOSITORY https://github.com/hanickadot/compile-time-regular-expressions.git
    GIT_TAG 78c5c3ef0e562f2ef0fea9ee3de743cdcf99e6f2  # v3.9.0
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(ctre)
endif()

# Pinned to 3.12.0 deliberately: that is the version the first consuming platform
# vendors, so the SDK builds against the copy it already has rather than dragging
# in a second one.
if(CE_DEFAULT_CODEC)
  find_package(nlohmann_json 3.12.0 QUIET)
  if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b  # v3.12.0
      GIT_SHALLOW FALSE)
    # nlohmann's JSON_Install defaults to ${MAIN_PROJECT}, which is OFF when it is
    # fetched as a subproject. Without this the installed package is not
    # self-contained: cloudeventsConfig.cmake find_dependency()s nlohmann, nothing
    # staged it, and a consumer on a machine with no system nlohmann cannot
    # configure. CTRE already installs itself this way, so this makes the two
    # dependencies behave alike.
    set(JSON_Install ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

if(CE_BUILD_TESTING)
  find_package(ut QUIET)
  if(NOT ut_FOUND)
    # ut 2.3.1 stores argc/argv from a function marked
    # __attribute__((constructor(101))), guarded on compiler identity alone. Mach-O
    # supports plain constructors but not constructor *priorities*, so GCC on macOS
    # rejects it outright ("constructor priorities are not supported"). Clang on
    # macOS and every compiler on ELF are unaffected, which is why CI never sees it.
    #
    # Dropping the priority keeps the behaviour: 101 is the lowest user priority and
    # nothing here depends on ordering against other static initialisers, because
    # largc/largv are read later, at run time. The patch is applied only on the
    # affected combination so no other platform builds something different.
    FetchContent_Declare(ut
      GIT_REPOSITORY https://github.com/boost-ext/ut.git
      GIT_TAG f923e6fe4b7542d75e0c4ee54ad0af6a5382a87c  # v2.3.1
      GIT_SHALLOW FALSE)
    # The SDK uses the header, not the module (CLAUDE.md), so the module build is
    # off; and ut's own suite is not ours to run.
    set(BOOST_UT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BOOST_UT_DISABLE_MODULE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ut)

    # Patched in place rather than through PATCH_COMMAND, so the substitution is
    # visible at configure time and a failure to match is reported here instead of
    # surfacing as the original compile error.
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
