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
- A `// spec: <UID>` marker in a public header is evidence for `satisfied_by`, not
  `verified_by`, so it is checked separately and in both directions: the
  requirement must list `code:<that header>`, and a requirement listing a header
  must be marked somewhere in it. Either gap is an ERROR. The markers are the
  only comments the headers carry, so this is what keeps them from decaying into
  decoration.

Test identity is the boost-ext/ut SUITE name, the string literal in
`boost::ut::suite<"suite name">`. Individual `"case"_test` names inside a suite are
also indexed, so a requirement may name either, but a spec marker sits above a
suite declaration and resolves to the suite.

The performance tooling is Python, and its suites are `unittest` classes under
`bench/perf/`. There the identity is the TestCase class name (a `test_` method name
also resolves), and a `# spec: <UID>` marker sits above the class. Both directions
are checked exactly as for ut suites.

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

# Python unittest suites, which verify the performance job's tooling.
PY_TEST_DIRS = [REPO_ROOT / "bench" / "perf"]
PY_SUFFIXES = {".py"}
PY_CLASS_RE = re.compile(r"^class\s+(\w+)\s*\([^)]*TestCase[^)]*\)\s*:", re.MULTILINE)
PY_METHOD_RE = re.compile(r"^\s+def\s+(test_\w+)\s*\(", re.MULTILINE)

# The public headers, whose markers name the requirement an entity implements.
IMPLEMENTATION_DIR = REPO_ROOT / "include"
IMPLEMENTATION_SUFFIXES = {".hpp", ".cppm"}
IMPLEMENTATION_PREFIX = "code:include/"

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


def _py_names_in(text: str) -> set[str]:
    """Every unittest class and test method a `verified_by` entry may name."""
    return set(PY_CLASS_RE.findall(text)) | set(PY_METHOD_RE.findall(text))


def index_ut_tests() -> dict[str, set[str]]:
    """Map repo-relative file path -> set of suite names declared in it: ut suites
    in the C++ test trees, unittest classes in the Python ones."""
    index: dict[str, set[str]] = {}
    for directory in TEST_DIRS:
        for path in _walk(directory, CPP_SUFFIXES):
            text = path.read_text(encoding="utf-8", errors="ignore")
            names = _ut_names_in(text)
            if names:
                index[str(path.relative_to(REPO_ROOT))] = names
    for directory in PY_TEST_DIRS:
        for path in _walk(directory, PY_SUFFIXES):
            text = path.read_text(encoding="utf-8", errors="ignore")
            names = _py_names_in(text)
            if names:
                index[str(path.relative_to(REPO_ROOT))] = names
    return index


def _py_test_for_marker(lines: list[str], marker_line: int) -> str | None:
    """The unittest class a `# spec:` marker sits above."""
    for offset in range(0, 4):
        idx = marker_line + offset
        if idx >= len(lines):
            break
        found = PY_CLASS_RE.search(lines[idx])
        if found:
            return found.group(1)
    return None


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
            # The name still has to exist: skipping the check outright let a
            # requirement point at a target nobody had written, and the gate
            # reported nothing.
            if path.endswith("CMakeLists.txt"):
                text = (REPO_ROOT / path).read_text(encoding="utf-8")
                if name not in text:
                    errors.append(
                        f"{rel}: verified_by '{ref}' names nothing in {path}. "
                        f"A build-level check must appear there by that exact name."
                    )
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

    marked_files = [
        (path, _ut_test_for_marker, "ut suite")
        for directory in TEST_DIRS
        for path in _walk(directory, CPP_SUFFIXES)
    ] + [
        (path, _py_test_for_marker, "unittest class")
        for directory in PY_TEST_DIRS
        for path in _walk(directory, PY_SUFFIXES)
    ]
    for path, suite_for_marker, kind in marked_files:
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
            suite = suite_for_marker(lines, line_no)
            if suite is None:
                warnings.append(
                    f"{rel}:{line_no + 1}: spec marker for '{uid}' is not attached to a {kind}"
                )
                continue
            qualified = f"{rel}::{suite}"
            if qualified not in verified_names[uid]:
                errors.append(
                    f"{rel}:{line_no + 1}: test '{suite}' cites {uid}, but "
                    f"{uid} does not list 'test:{qualified}' in verified_by"
                )

    return errors, warnings


def check_implementation_markers(reqs) -> list[str]:
    """Header markers and `satisfied_by` must name each other."""
    by_uid = {req.uid: req for req in reqs}
    marked: dict[str, set[str]] = {}
    errors: list[str] = []

    for path in _walk(IMPLEMENTATION_DIR, IMPLEMENTATION_SUFFIXES):
        rel = str(path.relative_to(REPO_ROOT))
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        for line_no, line in enumerate(lines, start=1):
            for uid in MARKER_RE.findall(line):
                if uid not in by_uid:
                    errors.append(f"{rel}:{line_no}: marker cites unknown requirement '{uid}'")
                    continue
                marked.setdefault(rel, set()).add(uid)
                if f"code:{rel}" not in (by_uid[uid].meta.get("satisfied_by") or []):
                    errors.append(
                        f"{rel}:{line_no}: marker cites {uid}, but {uid} does not list "
                        f"'code:{rel}' in satisfied_by"
                    )

    for req in reqs:
        if req.meta.get("status") == "obsolete":
            continue
        for ref in req.meta.get("satisfied_by") or []:
            if not ref.startswith(IMPLEMENTATION_PREFIX):
                continue
            header = ref.partition(":")[2]
            if req.uid not in marked.get(header, set()):
                errors.append(
                    f"{req.path.relative_to(REPO_ROOT)}: satisfied_by '{ref}', but {header} "
                    f"carries no '// spec: {req.uid}' marker"
                )

    return errors


def main() -> int:
    try:
        reqs = reqlib.load_requirements()
    except reqlib.FrontmatterError as exc:
        print(f"FAIL: {exc}")
        return 1

    ut_index = index_ut_tests()

    fwd_errors, fwd_warnings = check_forward(reqs, ut_index)
    rev_errors, rev_warnings = check_reverse(reqs, ut_index)
    impl_errors = check_implementation_markers(reqs)

    errors = fwd_errors + rev_errors + impl_errors
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
