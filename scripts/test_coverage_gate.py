"""Suites for scripts/check_coverage_gate.py (SWR-SEC-0007).

Registered with CTest as `coverage_gate_tools`; standard-library Python only:

    python3 -m unittest discover -s scripts -p 'test_coverage_gate*.py'

Each mutation case copies the four files the check reads into a throwaway tree and
breaks one thing, so a dropped flag, a hard-coded number or a floating toolchain is
shown to fail the check; the last suite asks whether this repository passes.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_coverage_gate as gate  # noqa: E402

FILES = (gate.CMAKE_FILE, gate.WORKFLOW_FILE, *gate.DOCUMENT_FILES)


def copy_of_repository(destination: pathlib.Path) -> None:
    for relative in FILES:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text((gate.REPO_ROOT / relative).read_text(encoding="utf-8"), encoding="utf-8")


def mutate(root: pathlib.Path, relative: pathlib.Path, old: str, new: str) -> None:
    path = root / relative
    text = path.read_text(encoding="utf-8")
    assert old in text, f"{relative} no longer contains {old!r}: update the case"
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


class Mutated:
    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.root = pathlib.Path(self._directory.name)
        copy_of_repository(self.root)

    def findings_after(self, relative: pathlib.Path, old: str, new: str) -> list[str]:
        mutate(self.root, relative, old, new)
        return gate.check(self.root)


# spec: SWR-SEC-0007
class TheBranchFlags(Mutated, unittest.TestCase):
    def test_the_copy_is_clean(self) -> None:
        self.assertEqual(gate.check(self.root), [])

    def test_dropping_the_throw_exclusion_fails(self) -> None:
        found = self.findings_after(gate.CMAKE_FILE, "--exclude-throw-branches", "")
        self.assertTrue(any("--exclude-throw-branches" in line for line in found), found)

    def test_dropping_the_branch_gate_fails(self) -> None:
        found = self.findings_after(gate.CMAKE_FILE, "--fail-under-branch", "--print-summary")
        self.assertTrue(any("--fail-under-branch" in line for line in found), found)

    def test_a_literal_branch_floor_fails(self) -> None:
        found = self.findings_after(
            gate.CMAKE_FILE, f"--fail-under-branch ${{{gate.BRANCH_CONSTANT}}}", "--fail-under-branch 50")
        self.assertTrue(any(gate.BRANCH_CONSTANT in line for line in found), found)

    def test_dropping_the_line_gate_fails(self) -> None:
        found = self.findings_after(gate.CMAKE_FILE, "--fail-under-line", "--print-summary")
        self.assertTrue(any("--fail-under-line" in line for line in found), found)


# spec: SWR-SEC-0007
class TheFloorConstant(Mutated, unittest.TestCase):
    def test_a_missing_branch_floor_variable_fails(self) -> None:
        found = self.findings_after(
            gate.CMAKE_FILE, f"set({gate.BRANCH_CONSTANT}", "set(CE_SOMETHING_ELSE")
        self.assertTrue(any(gate.BRANCH_CONSTANT in line for line in found), found)

    def test_a_floor_that_is_not_a_percent_fails(self) -> None:
        text = (self.root / gate.CMAKE_FILE).read_text(encoding="utf-8")
        value = gate.cache_value(text, gate.BRANCH_CONSTANT)
        self.assertIsNotNone(value)
        for bad in ("0", "101", "eighty"):
            with self.subTest(bad=bad):
                directory = tempfile.TemporaryDirectory()
                self.addCleanup(directory.cleanup)
                root = pathlib.Path(directory.name)
                copy_of_repository(root)
                mutate(root, gate.CMAKE_FILE, f"{gate.BRANCH_CONSTANT} {value} ", f"{gate.BRANCH_CONSTANT} {bad} ")
                self.assertTrue(any("not a percent" in line for line in gate.check(root)))

    def test_the_documents_name_the_constant_and_not_the_number(self) -> None:
        for relative in gate.DOCUMENT_FILES:
            with self.subTest(document=str(relative)):
                path = self.root / relative
                path.write_text(
                    path.read_text(encoding="utf-8").replace(gate.BRANCH_CONSTANT, "the branch floor"),
                    encoding="utf-8")
                self.assertTrue(any(str(relative) in line for line in gate.check(self.root)))


# spec: SWR-SEC-0007
class ThePinnedToolchain(Mutated, unittest.TestCase):
    def test_a_job_without_the_gcovr_version_fails(self) -> None:
        found = self.findings_after(gate.WORKFLOW_FILE, f"      {gate.GCOVR_ENV}:", "      NOT_GCOVR:")
        self.assertTrue(any(gate.GCOVR_ENV in line for line in found), found)

    def test_a_literal_gcov_fails(self) -> None:
        found = self.findings_after(gate.WORKFLOW_FILE, f"gcov-${{{gate.GCC_ENV}}}", "gcov-13")
        self.assertTrue(any("literally" in line for line in found), found)

    def test_a_job_without_the_version_assertion_fails(self) -> None:
        found = self.findings_after(
            gate.WORKFLOW_FILE, f"- name: {gate.TOOLCHAIN_STEP}", "- name: Something else")
        self.assertTrue(any(gate.TOOLCHAIN_STEP in line for line in found), found)

    def test_an_assertion_that_cannot_fail_is_refused(self) -> None:
        text = (self.root / gate.WORKFLOW_FILE).read_text(encoding="utf-8")
        start = text.index(f"- name: {gate.TOOLCHAIN_STEP}")
        end = text.index("\n      - ", start)
        step = text[start:end]
        mutate(self.root, gate.WORKFLOW_FILE, step, step.replace("exit 1", "true"))
        self.assertTrue(any("exit 1" in line for line in gate.check(self.root)))

    def test_the_assertion_must_run_before_the_gate(self) -> None:
        text = (self.root / gate.WORKFLOW_FILE).read_text(encoding="utf-8")
        start = text.index(f"- name: {gate.TOOLCHAIN_STEP}")
        end = text.index("\n      - ", start)
        step = text[start:end]
        reordered = text.replace(step, "", 1)
        after_gate = reordered.index("      - uses: actions/upload-artifact@v4", reordered.index(gate.GATE_STEP))
        (self.root / gate.WORKFLOW_FILE).write_text(
            reordered[:after_gate] + "      " + step.lstrip() + "\n" + reordered[after_gate:], encoding="utf-8")
        self.assertTrue(any("must run before" in line for line in gate.check(self.root)))

    def test_a_job_summary_without_both_percentages_fails(self) -> None:
        found = self.findings_after(gate.WORKFLOW_FILE, "branch-rate", "line-rate")
        self.assertTrue(any(gate.SUMMARY_STEP in line for line in found), found)


# spec: SWR-SEC-0007
class TheExitStatus(Mutated, unittest.TestCase):
    def test_a_missing_file_exits_with_its_own_status(self) -> None:
        (self.root / gate.CMAKE_FILE).unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(gate.main(["--root", str(self.root)]), gate.EXIT_UNREADABLE)

    def test_a_disagreement_exits_with_its_own_status(self) -> None:
        mutate(self.root, gate.CMAKE_FILE, "--exclude-throw-branches", "")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(gate.main(["--root", str(self.root)]), gate.EXIT_DISAGREE)


# spec: SWR-SEC-0007
class ThisRepository(unittest.TestCase):
    def test_the_gate_is_configured_and_the_toolchain_pinned(self) -> None:
        self.assertEqual(gate.check(gate.REPO_ROOT), [])


if __name__ == "__main__":
    unittest.main()
