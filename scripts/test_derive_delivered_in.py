"""Suites for spec/tools/derive_delivered_in.py (SWR-BUILD-0016).

Registered with CTest as `mutation_tools` (the scripts directory's suites);
standard-library Python plus pyyaml, which the spec tools need anyway:

    python3 -m unittest discover -s scripts -p 'test_*.py'

Every case builds a throwaway git repository with tagged releases, so the
tool is exercised against real tags and never against this repository.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import subprocess
import sys
import tempfile
import unittest

SPEC_TOOLS = pathlib.Path(__file__).resolve().parent.parent / "spec" / "tools"
sys.path.insert(0, str(SPEC_TOOLS))

try:
    import derive_delivered_in as tool  # noqa: E402
except ImportError as exc:  # pyyaml is absent
    raise unittest.SkipTest(f"spec tools need pyyaml: {exc}") from exc

FIRST, SECOND = "v0.1.0", "v0.2.0"
UID = "SWR-DEMO-0001"
REQUIREMENT_PATH = f"spec/requirements/software/{UID}.md"
TEST_PATH = "test/demo_test.cpp"
CODE_PATH = "include/demo.hpp"
SUITE = "demo-suite"
STATEMENT = "The demo shall do the one thing it is for."
GIT_IDENTITY = ["-c", "user.name=t", "-c", "user.email=t@example.invalid",
                "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false"]


def requirement(statement: str = STATEMENT, version: int = 1, status: str = "approved",
                verified_by: str = f"test:{TEST_PATH}::{SUITE}",
                satisfied_by: str = f"code:{CODE_PATH}") -> str:
    return (
        "---\n"
        f"uid: {UID}\n"
        "title: A demonstration requirement\n"
        "type: software\n"
        f"status: {status}\n"
        "priority: low\n"
        "rationale: >\n"
        "  Kept folded, with  odd   spacing, to prove nothing else is rewritten.\n"
        "verification_method: test\n"
        "security_classification: operational\n"
        "derived_from: [SYS-DEMO-0001]\n"
        f"satisfied_by: [{satisfied_by}]\n"
        f"verified_by: [{verified_by}]\n"
        "owner: someone\n"
        f"version: {version}\n"
        "---\n"
        f"{statement}\n"
    )


def suite_source(name: str = SUITE) -> str:
    return f'const boost::ut::suite<"{name}"> s = [] {{}};\n'


class Repo:
    """A throwaway git repository whose commits and tags the cases script."""

    def __init__(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._dir.name)
        self.git("init", "-q")

    def close(self) -> None:
        self._dir.cleanup()

    def git(self, *args: str) -> str:
        done = subprocess.run(["git", *GIT_IDENTITY, "-C", str(self.root), *args],
                              capture_output=True, text=True, check=True)
        return done.stdout

    def put(self, path: str, text: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def release(self, tag: str) -> None:
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", f"release {tag}")
        self.git("tag", tag)

    def derived(self) -> dict[str, str | None]:
        return {req.uid: tag for _, req, tag, _ in tool.derive_all(self.root)}

    def reason(self) -> str:
        return next(reason for _, req, _, reason in tool.derive_all(self.root) if req.uid == UID)

    def run(self, *flags: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = tool.main(["--repo", str(self.root), *flags])
        return code, out.getvalue()

    def text(self) -> str:
        return (self.root / REQUIREMENT_PATH).read_text(encoding="utf-8")


class Scenario:
    """Mixin: a fresh repository per case."""

    repo: Repo

    def setUp(self) -> None:
        self.repo = Repo()
        self.addCleanup(self.repo.close)  # type: ignore[attr-defined]

    def ship_in(self, tag: str, req: str | None = None) -> None:
        """Release `tag` with the code, the suite and (optionally new) requirement text."""
        self.repo.put(CODE_PATH, "// code\n")
        self.repo.put(TEST_PATH, suite_source())
        self.repo.put(REQUIREMENT_PATH, req if req is not None else requirement())
        self.repo.release(tag)


# spec: SWR-BUILD-0016
class FirstTagThatHoldsEverything(Scenario, unittest.TestCase):
    def test_tests_that_appear_only_later_give_the_later_tag(self) -> None:
        self.repo.put(REQUIREMENT_PATH, requirement())
        self.repo.release(FIRST)
        self.ship_in(SECOND)
        self.assertEqual(self.repo.derived(), {UID: SECOND})

    def test_the_first_tag_that_holds_everything_wins_over_later_ones(self) -> None:
        self.ship_in(FIRST)
        self.repo.release(SECOND)
        self.assertEqual(self.repo.derived(), {UID: FIRST})

    def test_a_code_file_that_moved_after_the_release_does_not_move_the_tag(self) -> None:
        self.ship_in(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement(satisfied_by="code:include/moved.hpp"))
        self.assertEqual(self.repo.derived(), {UID: FIRST})

    def test_a_file_that_declares_no_suite_resolves_by_existing(self) -> None:
        self.repo.put("fuzz/fuzz_demo.cpp", "int LLVMFuzzerTestOneInput();\n")
        self.ship_in(FIRST, requirement(verified_by="test:fuzz/fuzz_demo.cpp::fuzz-demo"))
        self.assertEqual(self.repo.derived(), {UID: FIRST})

    def test_an_acceptance_page_resolves_once_it_exists(self) -> None:
        self.repo.release(FIRST)
        self.repo.put("docs/acceptance/scenario-a.md", "page\n")
        self.ship_in(SECOND, requirement(verified_by="acc:scenario-a"))
        self.assertEqual(self.repo.derived(), {UID: SECOND})

    def test_a_cmake_name_resolves_only_where_the_file_mentions_it(self) -> None:
        self.repo.put("CMakeLists.txt", "add_test(NAME other)\n")
        self.repo.put(REQUIREMENT_PATH, requirement(verified_by="test:CMakeLists.txt::build-check"))
        self.repo.release(FIRST)
        self.repo.put("CMakeLists.txt", "add_test(NAME build-check)\n")
        self.repo.release(SECOND)
        self.assertEqual(self.repo.derived(), {UID: SECOND})

    def test_a_reflowed_statement_still_counts_as_the_same_statement(self) -> None:
        self.ship_in(FIRST, requirement(statement="The demo shall\n  do the one   thing it is for."))
        self.assertEqual(self.repo.derived(), {UID: FIRST})


# spec: SWR-BUILD-0016
class NoTagQualifies(Scenario, unittest.TestCase):
    def test_a_requirement_amended_after_delivery_has_no_tag(self) -> None:
        self.ship_in(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement(statement="The demo shall do a second thing."))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("statement differs", self.repo.reason())

    def test_a_version_bump_after_delivery_has_no_tag(self) -> None:
        self.ship_in(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement(version=2))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("version 1, now 2", self.repo.reason())

    def test_a_verified_by_suite_no_tag_declares_has_no_tag(self) -> None:
        self.ship_in(FIRST, requirement(verified_by=f"test:{TEST_PATH}::no-such-suite"))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("name absent", self.repo.reason())

    def test_a_verified_by_file_no_tag_holds_has_no_tag(self) -> None:
        self.ship_in(FIRST, requirement(verified_by="test:test/absent_test.cpp::x"))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("file absent", self.repo.reason())

    def test_a_reference_that_names_no_file_has_no_tag(self) -> None:
        self.ship_in(FIRST, requirement(verified_by="test:bare_name"))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("not path-qualified", self.repo.reason())

    def test_an_unknown_reference_kind_has_no_tag(self) -> None:
        self.ship_in(FIRST, requirement(verified_by="web:somewhere"))
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("not a known reference", self.repo.reason())

    def test_a_requirement_file_born_after_the_last_tag_has_no_tag(self) -> None:
        self.repo.put(CODE_PATH, "// code\n")
        self.repo.put(TEST_PATH, suite_source())
        self.repo.release(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement())
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertIn("requirement file absent", self.repo.reason())

    def test_a_repository_without_tags_derives_nothing(self) -> None:
        self.repo.put(REQUIREMENT_PATH, requirement())
        self.assertEqual(self.repo.derived(), {UID: None})
        self.assertEqual(self.repo.reason(), "no release tags")


# spec: SWR-BUILD-0016
class WriteMode(Scenario, unittest.TestCase):
    def test_only_the_status_line_changes_and_one_line_is_added(self) -> None:
        self.ship_in(FIRST)
        before = self.repo.text()
        code, _ = self.repo.run("--write")
        after = self.repo.text()
        self.assertEqual(code, tool.EXIT_OK)
        expected = before.replace("status: approved\n", f"status: implemented\ndelivered_in: {FIRST}\n", 1)
        self.assertEqual(after, expected)
        self.assertEqual(len(after.splitlines()), len(before.splitlines()) + 1)

    def test_a_requirement_with_no_tag_stays_approved_byte_for_byte(self) -> None:
        self.ship_in(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement(version=2))
        before = self.repo.text()
        self.repo.run("--write")
        self.assertEqual(self.repo.text(), before)

    def test_an_implemented_requirement_is_left_alone(self) -> None:
        self.ship_in(FIRST, requirement(status="implemented"))
        before = self.repo.text()
        self.repo.run("--write")
        self.assertEqual(self.repo.text(), before)

    def test_a_second_write_changes_nothing(self) -> None:
        self.ship_in(FIRST)
        self.repo.run("--write")
        once = self.repo.text()
        self.repo.run("--write")
        self.assertEqual(self.repo.text(), once)

    def test_the_table_names_the_tag_or_the_reason(self) -> None:
        self.ship_in(FIRST)
        _, delivered = self.repo.run()
        self.assertRegex(delivered, rf"{UID}\s+approved\s+{FIRST}")
        self.repo.put(REQUIREMENT_PATH, requirement(version=2))
        _, undelivered = self.repo.run()
        self.assertRegex(undelivered, rf"{UID}\s+approved\s+none: at {FIRST}: version 1, now 2")


# spec: SWR-BUILD-0016
class CheckMode(Scenario, unittest.TestCase):
    def test_check_fails_while_a_write_would_change_something(self) -> None:
        self.ship_in(FIRST)
        before = self.repo.text()
        code, out = self.repo.run("--check")
        self.assertEqual(code, tool.EXIT_DRIFT)
        self.assertIn(REQUIREMENT_PATH, out)
        self.assertEqual(self.repo.text(), before, "--check must not edit anything")

    def test_check_passes_once_the_write_was_applied(self) -> None:
        self.ship_in(FIRST)
        self.repo.run("--write")
        code, _ = self.repo.run("--check")
        self.assertEqual(code, tool.EXIT_OK)

    def test_check_passes_when_only_undelivered_requirements_remain(self) -> None:
        self.ship_in(FIRST)
        self.repo.put(REQUIREMENT_PATH, requirement(version=2))
        code, _ = self.repo.run("--check")
        self.assertEqual(code, tool.EXIT_OK)


if __name__ == "__main__":
    unittest.main()
