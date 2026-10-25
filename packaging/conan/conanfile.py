"""Conan recipe for cloudevents-cpp (SWR-ADOPT-0007).

Built from a clone, not from ConanCenter:

    conan create packaging/conan --build=missing

The sources are the clone this recipe sits in: `export_sources` copies them from
two directories up, so `conan create` builds the working tree, and `set_version`
reads the version from the project's CMakeLists.txt, so a release changes one
number (SWR-ADOPT-0008 checks that this stays true).

The package installs the project's own cloudeventsConfig.cmake, and Conan's
CMakeDeps generates nothing for it (`cmake_find_mode` none). That keeps the
targets the CMake install defines, ce::core, ce::format_protobuf and the others,
and the `codec_nlohmann` component that config reports, so a consumer is written once
and builds the same whichever way the SDK arrived.
"""

import os
import re

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load

# The floor the SDK builds against: cmake/CeDependencies.cmake pins CTRE 3.9.0
# and asks nlohmann_json for 3.12.0. A range, because a header-only dependency
# carries no ABI, and the major version bounds a break.
CTRE_REQUIREMENT = "ctre/[>=3.9.0 <4]"
NLOHMANN_REQUIREMENT = "nlohmann_json/[>=3.12.0 <4]"
# RapidJSON has no release numbers on ConanCenter, only dated revisions.
RAPIDJSON_REQUIREMENT = "rapidjson/cci.20250205"
# CE_BOOST_MINIMUM in the project's CMakeLists.txt.
BOOST_REQUIREMENT = "boost/[>=1.81 <2]"

# One option per codec, mapped to the name CE_CODECS uses.
CODEC_OPTIONS = {"nlohmann": "nlohmann", "rapidjson": "rapidjson", "boost_json": "boost_json"}

PROJECT_VERSION = re.compile(r"project\(\s*cloudevents\s+VERSION\s+(\d+\.\d+\.\d+)")


class CloudeventsCppConan(ConanFile):
    name = "cloudevents-cpp"
    license = "Apache-2.0"
    url = "https://github.com/filipsajdak/cloudevents-sdk-cpp"
    homepage = "https://github.com/filipsajdak/cloudevents-sdk-cpp"
    description = "CloudEvents v1.0.2 SDK for C++20: header-only, exception-free, JSON codecs optional."
    topics = ("cloudevents", "events", "header-only")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {codec: [True, False] for codec in CODEC_OPTIONS}
    # nlohmann is on by default, as it is in CE_CODECS. boost_json needs a
    # compiled Boost.JSON, so it is declared here and not built by CI.
    default_options = {"nlohmann": True, "rapidjson": False, "boost_json": False}
    no_copy_source = True

    @property
    def _project_root(self):
        return os.path.join(self.recipe_folder, "..", "..")

    @property
    def _codecs(self):
        return [codec for option, codec in CODEC_OPTIONS.items() if self.options.get_safe(option)]

    def set_version(self):
        cmakelists = os.path.join(self._project_root, "CMakeLists.txt")
        if os.path.exists(cmakelists):
            self.version = PROJECT_VERSION.search(load(self, cmakelists)).group(1)

    def export_sources(self):
        for name in ("CMakeLists.txt", "LICENSE"):
            copy(self, name, src=self._project_root, dst=self.export_sources_folder)
        for directory in ("cmake", "include"):
            copy(self, "*", src=os.path.join(self._project_root, directory),
                 dst=os.path.join(self.export_sources_folder, directory))

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        # transitive_headers: ce::core includes CTRE's headers and a codec
        # includes its library's, so a consumer compiles against them too.
        self.requires(CTRE_REQUIREMENT, transitive_headers=True)
        if self.options.nlohmann:
            self.requires(NLOHMANN_REQUIREMENT, transitive_headers=True)
        if self.options.rapidjson:
            self.requires(RAPIDJSON_REQUIREMENT, transitive_headers=True)
        if self.options.boost_json:
            self.requires(BOOST_REQUIREMENT, transitive_headers=True)

    def package_id(self):
        # Nothing here is compiled, so the compiler and the platform do not
        # distinguish packages. The options stay: the codec choice is written
        # into the installed cloudeventsConfig.cmake, and a package built for
        # nlohmann must not stand in for one built for RapidJSON.
        self.info.settings.clear()

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["CE_CODECS"] = ";".join(self._codecs)
        # CE_BUILD_TESTING defaults to ON for a top-level project, which would
        # fetch ut and build the suite.
        toolchain.cache_variables["CE_BUILD_TESTING"] = False
        toolchain.cache_variables["CE_WERROR"] = False
        # The project looks its dependencies up with find_package and falls back
        # to downloading them. Under Conan that would be a build that never used
        # the package's requirements, so a missed lookup is made an error.
        toolchain.cache_variables["CMAKE_REQUIRE_FIND_PACKAGE_ctre"] = True
        if self.options.nlohmann:
            toolchain.cache_variables["CMAKE_REQUIRE_FIND_PACKAGE_nlohmann_json"] = True
        toolchain.cache_variables["FETCHCONTENT_FULLY_DISCONNECTED"] = True
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        CMake(self).install()

    def package_info(self):
        self.cpp_info.libdirs = []
        self.cpp_info.bindirs = []
        # The project's own configuration file is the interface: see the module
        # docstring. builddirs is where CMakeDeps looks for it.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs.append(os.path.join("lib", "cmake", "cloudevents"))
