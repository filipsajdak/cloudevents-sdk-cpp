#!/usr/bin/env python3
"""Fail when a packaging recipe names a version other than the project's.

SWR-ADOPT-0008. The project VERSION in CMakeLists.txt is the one number a
release changes. The vcpkg manifest repeats it as a literal, because vcpkg
cannot read CMake, so it is compared; the Conan recipe has to read it from
CMakeLists.txt rather than carry its own, so the recipe is checked for doing so.

    python3 scripts/check_packaging_version.py [--root DIR]

Exit 0 when every recipe agrees, 1 with one line per disagreement naming the
file, 2 when a file it must read is missing or has no version.

The vcpkg SHA-512 is not checked: the archive it hashes exists only once the tag
does, so docs/RELEASE.md updates it after tagging.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

CMAKELISTS = pathlib.Path("CMakeLists.txt")
VCPKG_MANIFEST = pathlib.Path("packaging/vcpkg/ports/cloudevents-cpp/vcpkg.json")
CONANFILE = pathlib.Path("packaging/conan/conanfile.py")

EXIT_AGREE = 0
EXIT_DISAGREE = 1
EXIT_UNREADABLE = 2

PROJECT_VERSION = re.compile(r"project\(\s*cloudevents\s+VERSION\s+(\d+\.\d+\.\d+)")
# A class attribute `version = "..."` is a second copy of the number.
CONAN_LITERAL_VERSION = re.compile(r"^\s*version\s*=", re.MULTILINE)
CONAN_SET_VERSION = re.compile(r"def\s+set_version\s*\(")


class Unreadable(Exception):
    """A file the check needs is missing, or holds no version."""


def _read(root: pathlib.Path, relative: pathlib.Path) -> str:
    path = root / relative
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise Unreadable(f"{relative}: cannot read: {error}") from error


def project_version(root: pathlib.Path) -> str:
    found = PROJECT_VERSION.search(_read(root, CMAKELISTS))
    if not found:
        raise Unreadable(f"{CMAKELISTS}: no project(cloudevents VERSION x.y.z)")
    return found.group(1)


def check(root: pathlib.Path) -> list[str]:
    """One finding per disagreement, each beginning with the file it names."""
    expected = project_version(root)
    findings: list[str] = []

    try:
        manifest = json.loads(_read(root, VCPKG_MANIFEST))
    except json.JSONDecodeError as error:
        raise Unreadable(f"{VCPKG_MANIFEST}: not JSON: {error}") from error
    declared = manifest.get("version")
    if declared != expected:
        findings.append(
            f"{VCPKG_MANIFEST}: version is {declared!r}, the project's is {expected!r} "
            f"({CMAKELISTS})")

    recipe = _read(root, CONANFILE)
    if CONAN_LITERAL_VERSION.search(recipe):
        findings.append(
            f"{CONANFILE}: carries a literal version; it must read {CMAKELISTS} in set_version")
    if not CONAN_SET_VERSION.search(recipe) or CMAKELISTS.name not in recipe:
        findings.append(f"{CONANFILE}: does not read its version from {CMAKELISTS} in set_version")

    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=REPO_ROOT,
                        help="repository root (default: this checkout)")
    arguments = parser.parse_args(argv)

    try:
        findings = check(arguments.root)
    except Unreadable as error:
        print(f"check_packaging_version: {error}", file=sys.stderr)
        return EXIT_UNREADABLE
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return EXIT_DISAGREE
    print(f"check_packaging_version: the recipes agree on {project_version(arguments.root)}")
    return EXIT_AGREE


if __name__ == "__main__":
    sys.exit(main())
