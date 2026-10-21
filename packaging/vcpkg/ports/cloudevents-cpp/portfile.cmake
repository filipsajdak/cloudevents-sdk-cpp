# Overlay port for cloudevents-cpp (SWR-ADOPT-0006).
#
# Two sources, one build:
#
#   released   vcpkg_from_github, the archive of the tag v${VERSION}, checked
#              against the SHA-512 below. This is what a consumer gets.
#   working    when the environment names a source directory, that directory is
#              built instead. CI uses it to prove the tree under review; nothing
#              else should. vcpkg scrubs the environment of a port build, so the
#              variable must be named in VCPKG_KEEP_ENV_VARS, and the binary cache
#              does not hash the directory, so a local run needs
#              --binarysource=clear (README.md, "Installing").
#
# The release checklist updates VERSION in vcpkg.json at the version bump and the
# SHA512 here after the tag exists (docs/RELEASE.md, SWR-ADOPT-0008).

# Header-only: one build is the whole package, and a debug copy of an interface
# library would only be an empty directory vcpkg refuses.
set(VCPKG_BUILD_TYPE release)

if(DEFINED ENV{CLOUDEVENTS_CPP_SOURCE_DIR} AND NOT "$ENV{CLOUDEVENTS_CPP_SOURCE_DIR}" STREQUAL "")
    set(SOURCE_PATH "$ENV{CLOUDEVENTS_CPP_SOURCE_DIR}")
    message(STATUS "cloudevents-cpp: building the working tree at ${SOURCE_PATH}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO filipsajdak/cloudevents-sdk-cpp
        REF "v${VERSION}"
        SHA512 c3f3c556e378152b4acf5eabca4f9aa9a1a1761acc76f248adddc460ce806ad2974a2cd85f9a17f83c6513b48319dc049748b5099ad8c26d17c26a51b96feb10
        HEAD_REF main
    )
endif()

# One feature per codec, in the order CE_CODECS documents. A feature name cannot
# hold an underscore, so boost-json maps to boost_json.
set(codecs "")
set(require_find "")
if("nlohmann" IN_LIST FEATURES)
    list(APPEND codecs nlohmann)
    list(APPEND require_find -DCMAKE_REQUIRE_FIND_PACKAGE_nlohmann_json=ON)
endif()
if("rapidjson" IN_LIST FEATURES)
    list(APPEND codecs rapidjson)
endif()
if("boost-json" IN_LIST FEATURES)
    list(APPEND codecs boost_json)
endif()
# OPTIONS is a list, so the separator inside CE_CODECS has to be escaped.
list(JOIN codecs "\\;" ce_codecs)

# The project looks CTRE and nlohmann up with find_package and falls back to
# downloading them. Under vcpkg that fallback would be a build that never used
# the port's dependencies, so a missed lookup is made an error instead.
# CE_BUILD_TESTING defaults to ON for a top-level project and would fetch ut.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        "-DCE_CODECS=${ce_codecs}"
        -DCE_BUILD_TESTING=OFF
        -DCE_WERROR=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_ctre=ON
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        ${require_find}
)
vcpkg_cmake_install()

# The package is cloudevents, the port is cloudevents-cpp.
vcpkg_cmake_config_fixup(PACKAGE_NAME cloudevents CONFIG_PATH lib/cmake/cloudevents)

# Interface libraries install no library file, which leaves lib/ empty.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
