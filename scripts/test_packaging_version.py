"""Suites for scripts/check_packaging_version.py (SWR-ADOPT-0008).

Registered with CTest as `packaging_tools`; standard-library Python only:

    python3 -m unittest discover -s scripts -p 'test_packaging_*.py'

Each case writes a throwaway tree holding the three files the check reads, so the
check is exercised against versions that disagree and never against this
repository's, except in the one case that asks whether this repository agrees.
"""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_packaging_version as check  # noqa: E402

PROJECT = "1.2.3"
READING_RECIPE = (
    "class Recipe:\n"
    "    def set_version(self):\n"
    "        text = load(self, os.path.join(root, 'CMakeLists.txt'))\n"
)


def write_tree(root: pathlib.Path, *, cmake: str = PROJECT, vcpkg: object = PROJECT,
               recipe: str = READING_RECIPE) -> None:
    (root / check.CMAKELISTS).write_text(
        f"project(cloudevents\n  VERSION {cmake}\n  LANGUAGES CXX)\n", encoding="utf-8")
    manifest = root / check.VCPKG_MANIFEST
    manifest.parent.mkdir(parents=True)
    manifest.write_text(json.dumps({"name": "cloudevents-cpp", "version": vcpkg}), encoding="utf-8")
    conanfile = root / check.CONANFILE
    conanfile.parent.mkdir(parents=True)
    conanfile.write_text(recipe, encoding="utf-8")


# spec: SWR-ADOPT-0008
class PackagingVersion(unittest.TestCase):
    def setUp(self) -> None:
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        self.root = pathlib.Path(holder.name)

    def run_main(self) -> tuple[int, str]:
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors), contextlib.redirect_stdout(io.StringIO()):
            status = check.main(["--root", str(self.root)])
        return status, errors.getvalue()

    def test_agreeing_recipes_pass(self) -> None:
        write_tree(self.root)
        self.assertEqual(check.check(self.root), [])
        self.assertEqual(self.run_main()[0], check.EXIT_AGREE)

    def test_a_vcpkg_version_behind_the_project_fails_naming_the_manifest(self) -> None:
        write_tree(self.root, vcpkg="1.2.2")
        status, message = self.run_main()
        self.assertEqual(status, check.EXIT_DISAGREE)
        self.assertIn(str(check.VCPKG_MANIFEST), message)
        self.assertIn("1.2.2", message)
        self.assertIn(PROJECT, message)

    def test_a_vcpkg_manifest_without_a_version_fails(self) -> None:
        write_tree(self.root, vcpkg=None)
        self.assertEqual(self.run_main()[0], check.EXIT_DISAGREE)

    def test_a_conan_recipe_with_its_own_version_fails_naming_the_recipe(self) -> None:
        write_tree(self.root, recipe=READING_RECIPE + "    version = '1.2.3'\n")
        status, message = self.run_main()
        self.assertEqual(status, check.EXIT_DISAGREE)
        self.assertIn(str(check.CONANFILE), message)

    def test_a_conan_recipe_that_does_not_read_cmake_fails(self) -> None:
        write_tree(self.root, recipe="class Recipe:\n    def set_version(self):\n        pass\n")
        status, message = self.run_main()
        self.assertEqual(status, check.EXIT_DISAGREE)
        self.assertIn(str(check.CONANFILE), message)

    def test_a_conan_recipe_without_set_version_fails(self) -> None:
        write_tree(self.root, recipe="# reads CMakeLists.txt, but never in set_version\n")
        self.assertEqual(self.run_main()[0], check.EXIT_DISAGREE)

    def test_both_recipes_wrong_report_both(self) -> None:
        write_tree(self.root, vcpkg="0.0.1", recipe="version = '0.0.1'\n")
        findings = check.check(self.root)
        self.assertTrue(any(str(check.VCPKG_MANIFEST) in line for line in findings))
        self.assertTrue(any(str(check.CONANFILE) in line for line in findings))

    def test_a_missing_file_is_not_a_pass(self) -> None:
        write_tree(self.root)
        (self.root / check.CONANFILE).unlink()
        status, message = self.run_main()
        self.assertEqual(status, check.EXIT_UNREADABLE)
        self.assertIn(str(check.CONANFILE), message)

    def test_a_project_without_a_version_is_not_a_pass(self) -> None:
        write_tree(self.root)
        (self.root / check.CMAKELISTS).write_text("project(cloudevents)\n", encoding="utf-8")
        self.assertEqual(self.run_main()[0], check.EXIT_UNREADABLE)

    def test_this_repository_agrees(self) -> None:
        self.assertEqual(check.check(check.REPO_ROOT), [])


if __name__ == "__main__":
    unittest.main()
