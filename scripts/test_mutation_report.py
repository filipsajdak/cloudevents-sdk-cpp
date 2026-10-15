"""Suites for scripts/mutation_report.py, the mutation job's summary and comment.

Registered with CTest as `mutation_tools`; standard-library Python only.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutation_report  # noqa: E402

SCRIPT = Path(__file__).resolve().parent / "mutation_report.py"
VERDICT = "Mutation: Mull, changed lines: 7 killed, 2 survived (0 new tests, 0 equivalent, 0 arid)"
SURVIVOR = "survived: /r/include/cloudevents/detail/timestamp.hpp:9:7: Replaced > with >= [cxx_gt_to_ge]"
RAN = f"{SURVIVOR}\n{VERDICT}\n"


# spec: SWR-BUILD-0014
class ReportBody(unittest.TestCase):
    def test_a_run_shows_the_verdict_line_and_every_survivor(self) -> None:
        body = mutation_report.render(mutation_report.EXIT_OK, RAN)
        self.assertIn(VERDICT, body)
        self.assertIn("`include/cloudevents/detail/timestamp.hpp:9:7: Replaced > with >= [cxx_gt_to_ge]`", body)

    def test_the_comment_starts_with_the_marker_the_job_finds_it_by(self) -> None:
        body = mutation_report.render(mutation_report.EXIT_OK, RAN)
        self.assertEqual(body.splitlines()[0], mutation_report.MARKER)

    def test_every_outcome_says_a_surviving_mutant_never_fails_the_pull_request(self) -> None:
        for status in (0, 3, 4, 1):
            with self.subTest(status=status):
                self.assertIn(mutation_report.ADVISORY_NOTE, mutation_report.render(status, RAN))

    def test_survivors_beyond_the_cap_are_counted_not_listed(self) -> None:
        extra = 5
        text = "\n".join(
            f"{mutation_report.SURVIVED_PREFIX}f.hpp:{row}:1: Replaced > with >= [m]"
            for row in range(mutation_report.MAX_LISTED_SURVIVORS + extra)
        )
        body = mutation_report.render(mutation_report.EXIT_OK, text + "\n" + VERDICT)
        self.assertEqual(body.count("- `f.hpp:"), mutation_report.MAX_LISTED_SURVIVORS)
        self.assertIn(f"and {extra} more", body)

    def test_no_survivors_means_no_survivor_section(self) -> None:
        self.assertNotIn("<details>", mutation_report.render(mutation_report.EXIT_OK, VERDICT + "\n"))

    def test_a_change_without_include_lines_says_there_is_nothing_to_mutate(self) -> None:
        body = mutation_report.render(mutation_report.EXIT_OK, mutation_report.NOTHING_TO_MUTATE + " under include/\n")
        self.assertIn("Nothing to mutate", body)
        self.assertNotIn("Mull ran on", body)

    def test_changed_lines_with_no_mutant_are_explained_not_called_broken(self) -> None:
        body = mutation_report.render(mutation_report.EXIT_NO_MUTANTS, "Mutation: none, because x\n")
        self.assertIn("ran no mutant", body)
        self.assertNotIn("is broken", body)

    def test_a_broken_tool_is_named_and_its_log_tail_quoted(self) -> None:
        log = "\n".join(f"line {n}" for n in range(mutation_report.FAILURE_TAIL_LINES + 10))
        body = mutation_report.render(mutation_report.EXIT_NO_CONFIG, log)
        self.assertIn("job is broken", body)
        self.assertIn(f"line {mutation_report.FAILURE_TAIL_LINES + 9}", body)
        self.assertNotIn("line 9\n", body)

    def test_an_unexpected_status_is_reported_with_its_number(self) -> None:
        self.assertIn("status 7", mutation_report.render(7, ""))

    def test_the_base_and_the_job_log_are_linked_when_known(self) -> None:
        body = mutation_report.render(0, RAN, base_ref="main", run_url="https://example.invalid/run/1")
        self.assertIn("base `main`", body)
        self.assertIn("[job log](https://example.invalid/run/1)", body)


class Command(unittest.TestCase):
    def run_script(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True, check=False)

    def test_prints_the_report_and_exits_zero_whatever_the_status(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            log = Path(scratch) / "mutation.out"
            log.write_text(RAN)
            done = self.run_script("--status", "0", str(log))
        self.assertEqual(done.returncode, 0)
        self.assertIn(VERDICT, done.stdout)

    def test_a_missing_log_still_yields_a_report(self) -> None:
        done = self.run_script("--status", "1", "/nonexistent/mutation.out")
        self.assertEqual(done.returncode, 0)
        self.assertIn("status 1", done.stdout)


if __name__ == "__main__":
    unittest.main()
