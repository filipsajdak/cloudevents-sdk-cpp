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
if(CE_CODEC_NLOHMANN)
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

if(CE_CODEC_RAPIDJSON)
  # Found by header, never by find_package(RapidJSON). The config module
  # RapidJSON installs reports the include directory of the tree it was
  # CONFIGURED in, which on any other machine is a path that does not exist, and
  # it defines no imported target at all.
  find_path(CE_RAPIDJSON_INCLUDE_DIR rapidjson/document.h)
  if(NOT CE_RAPIDJSON_INCLUDE_DIR)
    # SOURCE_SUBDIR names a directory with no CMakeLists.txt deliberately:
    # RapidJSON is header-only, and configuring its own build would add its
    # tests, docs and install rules to this tree.
    #
    # A master commit rather than the v1.1.0 tag: that tag is from 2016, trips
    # -Wclass-memaccess on any current GCC, and predates the Parse(ptr, len)
    # overload this codec needs for a string_view that is not null-terminated.
    FetchContent_Declare(rapidjson
      GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
      GIT_TAG 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
      GIT_SHALLOW FALSE
      SOURCE_SUBDIR ce-does-not-configure-rapidjson)
    FetchContent_MakeAvailable(rapidjson)
    set(CE_RAPIDJSON_INCLUDE_DIR "${rapidjson_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  endif()

  add_library(ce_dep_rapidjson INTERFACE)
  add_library(ce_dep::rapidjson ALIAS ce_dep_rapidjson)
  # SYSTEM so RapidJSON's own warnings are not ours: the project builds with
  # -Wconversion -Wold-style-cast -Werror and RapidJSON does not.
  target_include_directories(ce_dep_rapidjson SYSTEM INTERFACE ${CE_RAPIDJSON_INCLUDE_DIR})
  # SYSTEM is not enough on its own. GenericMemberIterator derives from
  # std::iterator, deprecated in C++17, and the diagnostic fires when the
  # template is INSTANTIATED from our code - an instantiation context that
  # -isystem does not cover, so Clang reports it and CE_WERROR stops the build.
  # GCC happens not to, which is why this only appeared on the second compiler.
  #
  # RapidJSON's own escape hatch: the member iterator becomes a plain pointer
  # and std::iterator is never named. Nothing in the codec depends on the
  # iterator being a class.
  target_compile_definitions(ce_dep_rapidjson INTERFACE RAPIDJSON_NOMEMBERITERATORCLASS)
endif()

if(CE_CODEC_BOOST_JSON)
  # Boost.JSON is a COMPILED library, unlike every other dependency here, so
  # there is no FetchContent fallback:
  #
  #   - an INTERFACE target cannot supply the translation unit it needs, and
  #     asking every consumer to add one - in exactly one TU per shared object -
  #     is an ODR trap rather than a convenience;
  #   - standalone (header-only) mode was removed upstream in 1.81;
  #   - the Boost superproject is gigabytes and pulls its whole dependency
  #     closure into this build.
  #
  # A request we cannot honour should fail at configure time naming the package
  # to install, rather than half-working.
  # Boost requires the program to define boost::throw_exception under
  # -fno-exceptions, which is an application's policy and not this SDK's to
  # choose. codec/boost_json.hpp refuses that combination with an #error; saying
  # so here as well means the failure names the option rather than arriving as a
  # compile error deep inside a test.
  if(CMAKE_CXX_FLAGS MATCHES "-fno-exceptions")
    message(FATAL_ERROR
      "ce: CE_CODECS asks for boost_json in a build with -fno-exceptions. Boost "
      "then requires the program to define boost::throw_exception, which is an "
      "application policy decision this SDK will not make for you. Drop "
      "boost_json from CE_CODECS, or build with exceptions.")
  endif()

  find_package(Boost ${CE_BOOST_MINIMUM} QUIET COMPONENTS json)
  if(NOT TARGET Boost::json)
    message(FATAL_ERROR
      "ce: CE_CODECS asks for boost_json, which needs Boost ${CE_BOOST_MINIMUM} or newer with "
      "the compiled Boost.JSON library (libboost-json-dev on Debian, 'brew install boost' on "
      "macOS). There is no bundled copy; see docs/DECISIONS.md.")
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
