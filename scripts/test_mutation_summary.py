"""Suites for scripts/mutation_summary.py and the argument handling of mutation.sh.

Registered with CTest as `mutation_tools`; standard-library Python only:

    python3 -m unittest discover -s scripts -p 'test_*.py'
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutation_summary  # noqa: E402

SCRIPTS = Path(__file__).resolve().parent
HEADER = "/repo/include/cloudevents/detail/timestamp.hpp"


def line(verdict: str, row: int, mutator: str = "cxx_gt_to_ge") -> str:
    return f"{HEADER}:{row}:7: warning: {verdict}: Replaced > with >= [{mutator}]"


class Parse(unittest.TestCase):
    def test_reads_killed_and_survived_and_ignores_the_rest(self) -> None:
        text = "\n".join(["[info] Running mutants", line("Killed", 5), "    if (a > b) {", "          ^", line("Survived", 9)])
        verdicts = mutation_summary.parse(text)
        self.assertEqual(sorted(verdicts.values()), ["Killed", "Survived"])


class Merge(unittest.TestCase):
    def test_a_mutant_one_suite_kills_is_killed(self) -> None:
        missed = mutation_summary.parse(line("Survived", 5))
        caught = mutation_summary.parse(line("Killed", 5))
        for order in ([missed, caught], [caught, missed]):
            self.assertEqual(list(mutation_summary.merge(order).values()), ["Killed"])

    def test_a_mutant_every_suite_misses_survives(self) -> None:
        missed = mutation_summary.parse(line("Survived", 5))
        self.assertEqual(list(mutation_summary.merge([missed, missed]).values()), ["Survived"])

    def test_distinct_mutants_on_one_line_stay_distinct(self) -> None:
        both = mutation_summary.parse(line("Killed", 5) + "\n" + line("Survived", 5, "cxx_gt_to_le"))
        self.assertEqual(len(mutation_summary.merge([both])), 2)


class Summarise(unittest.TestCase):
    def test_the_verification_line(self) -> None:
        merged = mutation_summary.parse(line("Killed", 1) + "\n" + line("Killed", 2) + "\n" + line("Survived", 3))
        self.assertEqual(
            mutation_summary.summarise(merged, 1, 0, 1),
            "Mutation: Mull, changed lines: 2 killed, 1 survived (1 new tests, 0 equivalent, 1 arid)",
        )

    def test_no_reached_mutant_says_none_with_a_reason(self) -> None:
        self.assertTrue(mutation_summary.summarise({}, 0, 0, 0).startswith("Mutation: none, because"))

    def test_command_line_prints_survivors_then_the_line(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "suite.log"
            log.write_text(line("Killed", 1) + "\n" + line("Survived", 3) + "\n")
            run = subprocess.run(
                [sys.executable, str(SCRIPTS / "mutation_summary.py"), "--equivalent", "1", str(log)],
                capture_output=True, text=True, check=True,
            )
        lines = run.stdout.splitlines()
        self.assertTrue(lines[0].startswith("survived: " + HEADER + ":3:7"))
        self.assertEqual(
            lines[-1],
            "Mutation: Mull, changed lines: 1 killed, 1 survived (0 new tests, 1 equivalent, 0 arid)",
        )


# The script is bash, and the runners without a POSIX shell (MSVC) have none to run.
@unittest.skipUnless(os.name == "posix", "mutation.sh needs a POSIX shell")
class Script(unittest.TestCase):
    def run_script(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(["bash", str(SCRIPTS / "mutation.sh"), *args], capture_output=True, text=True)

    def test_no_base_ref_is_a_usage_error(self) -> None:
        run = self.run_script()
        self.assertEqual(run.returncode, 2)
        self.assertIn("usage:", run.stderr)

    def test_dry_run_prints_the_plan_and_builds_nothing(self) -> None:
        run = self.run_script("HEAD", "--dry-run", "--suites", "timestamp_test")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("gitDiffRef:", run.stdout)
        self.assertIn("timestamp_test", run.stdout)
        self.assertIn("mull-runner", run.stdout)


if __name__ == "__main__":
    unittest.main()
