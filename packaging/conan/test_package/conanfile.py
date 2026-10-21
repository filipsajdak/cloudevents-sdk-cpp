"""Builds test/consumer against the package, using only what Conan installed
(SWR-ADOPT-0007)."""

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class CloudeventsCppTestPackage(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        # A consumer that quietly fell back to something other than the package
        # would pass and prove nothing.
        toolchain.cache_variables["CMAKE_REQUIRE_FIND_PACKAGE_cloudevents"] = True
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not can_run(self):
            return
        cmake = CMake(self)
        cmake.ctest(cli_args=["--output-on-failure", "--no-tests=error"])
        # The codec tests exist only when the codec was found, and a consumer
        # that lost it would run fewer tests and still pass. Name them.
        options = self.dependencies[self.tested_reference_str].options
        named = []
        if options.nlohmann:
            named += ["consumer-codec-link", "consumer-roundtrip"]
        if options.rapidjson:
            named.append("consumer-roundtrip-rapidjson")
        for name in named:
            cmake.ctest(cli_args=["--output-on-failure", "--no-tests=error", "-R", f"^{name}$"])
