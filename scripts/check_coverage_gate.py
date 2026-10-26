#!/usr/bin/env python3
"""Check that the coverage gate is configured the way SWR-SEC-0007 says.

The gate is a CMake target and a CI job, so dropping a flag, hard-coding the number
or letting the toolchain float does not fail a build: the job would stay green and
quietly stop gating. This reads the three places the requirement lives and reports
every disagreement:

    test/CMakeLists.txt          CE_COVERAGE_BRANCH_FLOOR is a named cache variable and
                                 the coverage-report command carries both flags, each
                                 reading its floor from its constant and not a number
    .github/workflows/ci.yml     the `coverage floor` job names the gcovr version and
                                 the GCC major once, in its env, uses them everywhere
                                 else, and fails before measuring when the installed
                                 tools differ
    docs/RELEASE.md, CONTRIBUTING.md
                                 name the constant (the number lives only in CMake)

    python3 scripts/check_coverage_gate.py [--root DIR]

Exit 0 when everything agrees, 1 when something does not (one line each on stderr),
2 when a file cannot be read.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

EXIT_AGREE = 0
EXIT_DISAGREE = 1
EXIT_UNREADABLE = 2

CMAKE_FILE = pathlib.Path("test/CMakeLists.txt")
WORKFLOW_FILE = pathlib.Path(".github/workflows/ci.yml")
DOCUMENT_FILES = (pathlib.Path("docs/RELEASE.md"), pathlib.Path("CONTRIBUTING.md"))

JOB_NAME = "coverage floor"
LINE_CONSTANT = "CE_COVERAGE_FLOOR"
BRANCH_CONSTANT = "CE_COVERAGE_BRANCH_FLOOR"
GCOVR_ENV = "GCOVR_VERSION"
GCC_ENV = "GCC_MAJOR"
TOOLCHAIN_STEP = "Check the coverage toolchain"
GATE_STEP = "coverage-report"
SUMMARY_STEP = "Coverage summary"

PERCENT_MAX = 100

CACHE_VARIABLE = r"set\(\s*{name}\s+(\S+)\s+CACHE\s+STRING"
REPORT_TARGET = re.compile(r"add_custom_target\(coverage-report\b.*?COMMENT[^\n]*\)\n", re.DOTALL)
JOB_START = re.compile(r"^  [A-Za-z0-9_-]+:\s*$", re.MULTILINE)
LITERAL_TOOL = re.compile(r"(?:g\+\+|gcov)-\d+")


class Unreadable(Exception):
    """A file the check needs is missing, or lacks the block it reads."""


def _read(root: pathlib.Path, relative: pathlib.Path) -> str:
    try:
        return (root / relative).read_text(encoding="utf-8")
    except OSError as error:
        raise Unreadable(f"{relative}: cannot read: {error}") from error


def cache_value(cmake: str, name: str) -> str | None:
    found = re.search(CACHE_VARIABLE.format(name=re.escape(name)), cmake)
    return found.group(1) if found else None


def report_command(cmake: str) -> str:
    found = REPORT_TARGET.search(cmake)
    if not found:
        raise Unreadable(f"{CMAKE_FILE}: no add_custom_target(coverage-report ...)")
    return found.group(0)


def coverage_job(workflow: str) -> str:
    """The text of the `coverage floor` job: from its key to the next job's key."""
    name = re.search(rf"^    name: {re.escape(JOB_NAME)}\s*$", workflow, re.MULTILINE)
    if not name:
        raise Unreadable(f"{WORKFLOW_FILE}: no job named '{JOB_NAME}'")
    starts = [match.start() for match in JOB_START.finditer(workflow)]
    begin = max(start for start in starts if start < name.start())
    following = [start for start in starts if start > name.start()]
    return workflow[begin:min(following) if following else len(workflow)]


def cmake_findings(cmake: str) -> list[str]:
    findings: list[str] = []
    for name in (LINE_CONSTANT, BRANCH_CONSTANT):
        value = cache_value(cmake, name)
        if value is None:
            findings.append(f"{CMAKE_FILE}: {name} is not a `set(... CACHE STRING ...)` variable")
        elif not value.isdigit() or not 0 < int(value) <= PERCENT_MAX:
            findings.append(f"{CMAKE_FILE}: {name} is {value!r}, not a percent from 1 to {PERCENT_MAX}")
    command = report_command(cmake)
    if "--exclude-throw-branches" not in command:
        findings.append(f"{CMAKE_FILE}: coverage-report lacks --exclude-throw-branches")
    for flag, constant in (("--fail-under-branch", BRANCH_CONSTANT), ("--fail-under-line", LINE_CONSTANT)):
        if not re.search(rf"{re.escape(flag)}\s+\$\{{{constant}\}}", command):
            findings.append(f"{CMAKE_FILE}: coverage-report does not pass `{flag} ${{{constant}}}`")
    return findings


def workflow_findings(job: str) -> list[str]:
    findings: list[str] = []
    for name in (GCOVR_ENV, GCC_ENV):
        if not re.search(rf"^      {name}:\s*\S+", job, re.MULTILINE):
            findings.append(f"{WORKFLOW_FILE}: job '{JOB_NAME}' does not set {name} in its env")
    outside_env = re.sub(r"^      (?:GCOVR_VERSION|GCC_MAJOR):.*$", "", job, flags=re.MULTILINE)
    literal = LITERAL_TOOL.search(outside_env)
    if literal:
        findings.append(
            f"{WORKFLOW_FILE}: job '{JOB_NAME}' names {literal.group(0)} literally; "
            f"it must be built from {GCC_ENV}")
    check = re.search(rf"- name: {re.escape(TOOLCHAIN_STEP)}\n(.*?)(?=\n      - |\Z)", job, re.DOTALL)
    if not check:
        findings.append(f"{WORKFLOW_FILE}: job '{JOB_NAME}' has no step '{TOOLCHAIN_STEP}'")
    else:
        body = check.group(1)
        for needed in (GCOVR_ENV, GCC_ENV, "gcovr --version", "--version", "exit 1"):
            if needed not in body:
                findings.append(f"{WORKFLOW_FILE}: step '{TOOLCHAIN_STEP}' does not use {needed}")
        gate = job.find(f"- name: {GATE_STEP}")
        if gate == -1:
            findings.append(f"{WORKFLOW_FILE}: job '{JOB_NAME}' has no step '{GATE_STEP}'")
        elif gate < check.start():
            findings.append(f"{WORKFLOW_FILE}: '{TOOLCHAIN_STEP}' must run before '{GATE_STEP}'")
    summary = re.search(rf"- name: {re.escape(SUMMARY_STEP)}\n(.*?)(?=\n      - |\Z)", job, re.DOTALL)
    if not summary:
        findings.append(f"{WORKFLOW_FILE}: job '{JOB_NAME}' has no step '{SUMMARY_STEP}'")
    else:
        for needed in ("GITHUB_STEP_SUMMARY", "line-rate", "branch-rate"):
            if needed not in summary.group(1):
                findings.append(f"{WORKFLOW_FILE}: step '{SUMMARY_STEP}' does not use {needed}")
    return findings


def document_findings(root: pathlib.Path) -> list[str]:
    return [
        f"{path}: does not name {BRANCH_CONSTANT}"
        for path in DOCUMENT_FILES
        if BRANCH_CONSTANT not in _read(root, path)]


def check(root: pathlib.Path) -> list[str]:
    """One finding per disagreement, each beginning with the file it names."""
    return (cmake_findings(_read(root, CMAKE_FILE))
            + workflow_findings(coverage_job(_read(root, WORKFLOW_FILE)))
            + document_findings(root))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=REPO_ROOT,
                        help="repository root (default: this checkout)")
    arguments = parser.parse_args(argv)
    try:
        findings = check(arguments.root)
    except Unreadable as error:
        print(f"check_coverage_gate: {error}", file=sys.stderr)
        return EXIT_UNREADABLE
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return EXIT_DISAGREE
    print("check_coverage_gate: the line and branch gates are configured and the toolchain is pinned")
    return EXIT_AGREE


if __name__ == "__main__":
    sys.exit(main())
