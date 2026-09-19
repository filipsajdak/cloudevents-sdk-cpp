#!/usr/bin/env python3
"""Reference-resolution gate for requirement traceability, C++ / boost-ext ut flavour.

`check_trace.py` enforces that an approved requirement HAS a `verified_by` entry,
but not that anything real sits behind it. A renamed or deleted test would leave a
stale link the trace gate accepts, and the requirement would read as verified while
its evidence no longer exists.

This gate closes that hole in both directions:

- `satisfied_by` (`code:`/`doc:`) must resolve to a file that exists. Unresolved is
  an ERROR: an implemented requirement pointing at a moved header is a lie about
  where the behaviour lives.
- `verified_by` (`test:<path>::<name>`) unresolved is a WARNING. A spec-only branch
  legitimately names the test its implementation commit will add later, which is
  the workflow this repository uses.
- Every ut test carrying a `// spec: <UID>` marker MUST be listed in that
  requirement's `verified_by`. Unlisted is an ERROR. This is the direction that
  actually rots: a test gets written, the requirement is never updated, and the
  traceability matrix quietly under-reports coverage.

Test identity is the boost-ext/ut SUITE name, the string literal in
`boost::ut::suite<"suite name">`. Individual `"case"_test` names inside a suite are
also indexed, so a requirement may name either, but a spec marker sits above a
suite declaration and resolves to the suite.

Exit code 0 when there are no errors, 1 otherwise. Pure stdlib plus reqlib.
"""

from __future__ import annotations

import re
import sys

import reqlib

REPO_ROOT = reqlib.REPO_ROOT

# Directories that may carry a spec marker. Kept narrow so the scan stays quick and
# never wanders into a build tree or a fetched dependency.
TEST_DIRS = [REPO_ROOT / "test", REPO_ROOT / "fuzz", REPO_ROOT / "examples"]
SKIP_DIR_PARTS = {"_build", "build", ".venv", "venv", "_deps", "node_modules", ".git"}

CPP_SUFFIXES = {".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".h", ".inl"}

UID_RE = r"(?:STK|SYS|SWR)-[A-Z0-9]+-[0-9]{4}"
# Mirrors the `test_marker` form used by the other Scudo repositories.
#   // spec: SWR-CORE-0009
#   /* spec: SWR-CORE-0009 */
MARKER_RE = re.compile(rf"spec:\s*({UID_RE})")
# Two ut spellings, and BOTH are needed.
#
#   const boost::ut::suite<"core-timestamp-roundtrip"> ... = [] { ... };   <- the name
#   "canonical input round-trips"_test = [] { ... };                       <- a case inside it
#
# Requirements name the SUITE, and a spec marker sits above a suite declaration,
# which contains no `_test` at all. Matching only `_test` therefore matched
# nothing on a real suite file: every link resolved to "no ut suite in that file"
# and the reverse check never fired, so the traceability contract was inert.
UT_SUITE_RE = re.compile(r'\bsuite\s*<\s*"([^"]+)"\s*>')
UT_TEST_RE = re.compile(r'"([^"]+)"_test\b')


def _ut_names_in(text: str) -> set[str]:
    """Every name a `verified_by` entry may legitimately refer to in this file."""
    return set(UT_SUITE_RE.findall(text)) | set(UT_TEST_RE.findall(text))


def _walk(root, suffixes):
    if not root.is_dir():
        return
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in suffixes:
            continue
        if SKIP_DIR_PARTS & set(path.parts):
            continue
        yield path


def index_ut_tests() -> dict[str, set[str]]:
    """Map repo-relative file path -> set of ut suite names declared in it."""
    index: dict[str, set[str]] = {}
    for directory in TEST_DIRS:
        for path in _walk(directory, CPP_SUFFIXES):
            text = path.read_text(encoding="utf-8", errors="ignore")
            names = _ut_names_in(text)
            if names:
                index[str(path.relative_to(REPO_ROOT))] = names
    return index


def _ut_test_for_marker(lines: list[str], marker_line: int) -> str | None:
    """The ut suite a marker belongs to: the first suite declared on the marker's
    own line, or on one of the few lines below it (a marker sits above its suite).

    A suite declaration is preferred over a `_test` case on the same line, because
    a requirement's verified_by names the suite.
    """
    for offset in range(0, 4):
        idx = marker_line + offset
        if idx >= len(lines):
            break
        found = UT_SUITE_RE.search(lines[idx]) or UT_TEST_RE.search(lines[idx])
        if found:
            return found.group(1)
    return None


def check_forward(reqs, ut_index) -> tuple[list[str], list[str]]:
    """satisfied_by must resolve; verified_by unresolved is a warning."""
    errors: list[str] = []
    warnings: list[str] = []

    for req in reqs:
        rel = req.path.relative_to(REPO_ROOT)
        if req.meta.get("status") == "obsolete":
            continue

        for ref in req.meta.get("satisfied_by") or []:
            prefix, _, rest = ref.partition(":")
            if prefix in ("code", "doc") and not (REPO_ROOT / rest).exists():
                errors.append(f"{rel}: satisfied_by '{ref}' does not resolve to a file")

        for ref in req.meta.get("verified_by") or []:
            prefix, _, rest = ref.partition(":")
            if prefix != "test":
                continue
            if "::" not in rest:
                warnings.append(
                    f"{rel}: verified_by '{ref}' is not path-qualified "
                    "(expected test:<path>::<suite name>)"
                )
                continue
            path, name = rest.split("::", 1)
            if not (REPO_ROOT / path).exists():
                warnings.append(f"{rel}: verified_by '{ref}' path does not exist yet")
                continue
            # A CMake file names a build-level check rather than a ut suite.
            if path.endswith("CMakeLists.txt"):
                continue
            known = ut_index.get(path, set())
            if known and name not in known:
                errors.append(
                    f"{rel}: verified_by '{ref}' names no ut suite in {path}. "
                    f"The file exists, so this is a stale link rather than work not yet done."
                )

    return errors, warnings


def check_reverse(reqs, ut_index) -> tuple[list[str], list[str]]:
    """Every `// spec: <UID>` marked test must be listed in that spec's verified_by."""
    verified_names: dict[str, set[str]] = {}
    known_uids = {r.uid for r in reqs}
    for req in reqs:
        names = set()
        for ref in req.meta.get("verified_by") or []:
            if ref.startswith("test:"):
                names.add(ref.split(":", 1)[1].strip())
        verified_names[req.uid] = names

    errors: list[str] = []
    warnings: list[str] = []

    for directory in TEST_DIRS:
        for path in _walk(directory, CPP_SUFFIXES):
            rel = str(path.relative_to(REPO_ROOT))
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
            for line_no, line in enumerate(lines):
                found = MARKER_RE.search(line)
                if not found:
                    continue
                uid = found.group(1)
                if uid not in known_uids:
                    errors.append(f"{rel}:{line_no + 1}: marker cites unknown requirement '{uid}'")
                    continue
                suite = _ut_test_for_marker(lines, line_no)
                if suite is None:
                    warnings.append(
                        f"{rel}:{line_no + 1}: spec marker for '{uid}' is not attached to a ut suite"
                    )
                    continue
                qualified = f"{rel}::{suite}"
                if qualified not in verified_names[uid]:
                    errors.append(
                        f"{rel}:{line_no + 1}: test '{suite}' cites {uid}, but "
                        f"{uid} does not list 'test:{qualified}' in verified_by"
                    )

    return errors, warnings


def main() -> int:
    try:
        reqs = reqlib.load_requirements()
    except reqlib.FrontmatterError as exc:
        print(f"FAIL: {exc}")
        return 1

    ut_index = index_ut_tests()

    fwd_errors, fwd_warnings = check_forward(reqs, ut_index)
    rev_errors, rev_warnings = check_reverse(reqs, ut_index)

    errors = fwd_errors + rev_errors
    warnings = fwd_warnings + rev_warnings

    if warnings:
        print(f"WARN: {len(warnings)} non-blocking reference warning(s):")
        for warning in warnings:
            print(f"  - {warning}")

    if errors:
        print(f"FAIL: {len(errors)} reference error(s):")
        for error in errors:
            print(f"  - {error}")
        return 1

    tests = sum(len(v) for v in ut_index.values())
    print(f"OK: references resolve for {len(reqs)} requirement(s) and {tests} ut suite(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
