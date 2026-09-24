"""Suites for the performance job's comparison, budget and history tools.

Registered with CTest as `perf_tools`, so they run with the normal suites and
need none of the benchmark's dependencies:

    python3 -m unittest discover -s bench/perf -p 'test_*.py'

Each class verifies one requirement and names it in its `# spec:` marker.
"""

from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compare  # noqa: E402
import history  # noqa: E402
import measure  # noqa: E402
import seed_budgets  # noqa: E402

TOOLCHAIN = {"arch": "x86_64", "compiler": "g++-14 (Ubuntu) 14.2.0", "compiler_id": "gcc-14"}


def results(measurements: dict, sha: str = "head", toolchain: dict | None = None) -> dict:
    return {
        "schema": 1,
        "git_sha": sha,
        "date": "2026-09-24T12:00:00Z",
        "toolchain": dict(toolchain or TOOLCHAIN),
        "measurements": measurements,
    }


def budgets(limits: dict, toolchain: dict | None = None) -> dict:
    return {"toolchain": dict(toolchain or TOOLCHAIN), "budgets": limits}


def generous(measurements: dict) -> dict:
    """Budgets far above every gated measurement, so only the gate under test
    can fail."""
    return budgets({
        op_id: {m: v * 10 for m, v in values.items() if m in compare.GATES}
        for op_id, values in measurements.items()
    })


def gate(base: dict | None, head: dict, limits: dict | None = None) -> compare.Report:
    head_results = results(head)
    return compare.compare(
        results(base, sha="main") if base is not None else None,
        head_results,
        limits if limits is not None else generous(head),
    )


# spec: SWR-PERF-0001
class InstructionGate(unittest.TestCase):
    def test_a_small_increase_passes(self):
        report = gate({"op/c": {"instructions": 10000}}, {"op/c": {"instructions": 10100}})
        self.assertFalse(report.failed)

    def test_exactly_two_percent_passes(self):
        report = gate({"op/c": {"instructions": 10000}}, {"op/c": {"instructions": 10200}})
        self.assertFalse(report.failed)

    def test_just_over_two_percent_fails(self):
        report = gate({"op/c": {"instructions": 10000}}, {"op/c": {"instructions": 10201}})
        self.assertTrue(report.failed)
        self.assertIn("SWR-PERF-0001", report.failures[0])
        self.assertIn("+2.01%", report.failures[0])

    def test_a_decrease_passes(self):
        report = gate({"op/c": {"instructions": 10000}}, {"op/c": {"instructions": 9000}})
        self.assertFalse(report.failed)


# spec: SWR-PERF-0002
class AllocationGate(unittest.TestCase):
    def test_the_same_allocations_pass(self):
        same = {"op/c": {"allocations": 18, "allocated_bytes": 551}}
        self.assertFalse(gate(same, same).failed)

    def test_one_more_allocation_fails(self):
        report = gate({"op/c": {"allocations": 18, "allocated_bytes": 551}},
                      {"op/c": {"allocations": 19, "allocated_bytes": 551}})
        self.assertTrue(report.failed)
        self.assertIn("allocations", report.failures[0])
        self.assertIn("any increase", report.failures[0])

    def test_one_more_byte_fails(self):
        report = gate({"op/c": {"allocations": 18, "allocated_bytes": 551}},
                      {"op/c": {"allocations": 18, "allocated_bytes": 552}})
        self.assertTrue(report.failed)
        self.assertIn("SWR-PERF-0002", report.failures[0])

    def test_the_first_allocation_on_a_path_that_had_none_fails(self):
        report = gate({"op/c": {"allocations": 0}}, {"op/c": {"allocations": 1}},
                      budgets({"op/c": {"allocations": 5}}))
        self.assertTrue(report.failed)

    def test_fewer_allocations_pass(self):
        report = gate({"op/c": {"allocations": 18, "allocated_bytes": 551}},
                      {"op/c": {"allocations": 17, "allocated_bytes": 500}})
        self.assertFalse(report.failed)


# spec: SWR-PERF-0003
class RetainedGate(unittest.TestCase):
    def test_exactly_one_percent_passes(self):
        report = gate({"decode_full/c": {"retained_bytes": 1000}},
                      {"decode_full/c": {"retained_bytes": 1010}})
        self.assertFalse(report.failed)

    def test_just_over_one_percent_fails(self):
        report = gate({"decode_full/c": {"retained_bytes": 1000}},
                      {"decode_full/c": {"retained_bytes": 1011}})
        self.assertTrue(report.failed)
        self.assertIn("SWR-PERF-0003", report.failures[0])


# spec: SWR-PERF-0004
class BudgetGate(unittest.TestCase):
    def test_a_measurement_at_its_budget_passes(self):
        report = gate(None, {"op/c": {"instructions": 1100}},
                      budgets({"op/c": {"instructions": 1100}}))
        self.assertFalse(report.failed)

    def test_a_measurement_over_its_budget_fails(self):
        report = gate({"op/c": {"instructions": 1100}}, {"op/c": {"instructions": 1101}},
                      budgets({"op/c": {"instructions": 1100}}))
        self.assertTrue(report.failed)
        self.assertIn("over its budget", report.failures[0])
        self.assertIn("raise the budget in this pull request", report.failures[0])

    def test_a_measure_with_no_budget_fails_and_proposes_one(self):
        report = gate(None, {"new_op/c": {"allocations": 20}}, budgets({}))
        self.assertTrue(report.failed)
        self.assertIn("has no budget", report.failures[0])
        self.assertIn("Add one to `bench/budgets.json`", report.failures[0])
        self.assertIn(" 22 ", report.failures[0])

    def test_missing_base_gates_on_budgets_only(self):
        report = gate(None, {"op/c": {"instructions": 5000}},
                      budgets({"op/c": {"instructions": 6000}}))
        self.assertFalse(report.failed)
        self.assertIn("Only the budgets gate", report.notes[0])

    def test_missing_base_still_fails_over_budget(self):
        report = gate(None, {"op/c": {"instructions": 7000}},
                      budgets({"op/c": {"instructions": 6000}}))
        self.assertTrue(report.failed)

    def test_budgets_from_another_architecture_are_refused(self):
        arm = dict(TOOLCHAIN, arch="aarch64")
        report = gate(None, {"op/c": {"instructions": 5000}},
                      budgets({"op/c": {"instructions": 6000}}, toolchain=arm))
        self.assertTrue(report.failed)
        self.assertIn("aarch64", report.failures[0])
        self.assertIn("Re-seed", report.failures[0])

    def test_a_stale_budget_warns(self):
        report = gate(None, {"op/c": {"allocations": 1}},
                      budgets({"op/c": {"allocations": 1}, "gone/c": {"allocations": 1}}))
        self.assertFalse(report.failed)
        self.assertIn("gone/c", report.warnings[0])


# spec: SWR-PERF-0004
class AcceptedCost(unittest.TestCase):
    def compare(self, head_budget: int, base_budget: int | None) -> compare.Report:
        base_limits = {} if base_budget is None else {"op/c": {"allocations": base_budget}}
        return compare.compare(
            results({"op/c": {"allocations": 10}}, sha="main"),
            results({"op/c": {"allocations": 11}}),
            budgets({"op/c": {"allocations": head_budget}}),
            budgets(base_limits))

    def test_a_raised_budget_accepts_the_growth_over_main(self):
        report = self.compare(head_budget=13, base_budget=12)
        self.assertFalse(report.failed)
        self.assertIn("raised its budget to 13", report.accepted[0])

    def test_an_unchanged_budget_does_not(self):
        report = self.compare(head_budget=12, base_budget=12)
        self.assertTrue(report.failed)
        self.assertEqual(report.accepted, [])

    def test_without_mains_budgets_nothing_is_accepted(self):
        report = compare.compare(
            results({"op/c": {"allocations": 10}}, sha="main"),
            results({"op/c": {"allocations": 11}}),
            budgets({"op/c": {"allocations": 13}}))
        self.assertTrue(report.failed)

    def test_a_raised_budget_still_gates(self):
        report = compare.compare(
            results({"op/c": {"allocations": 10}}, sha="main"),
            results({"op/c": {"allocations": 14}}),
            budgets({"op/c": {"allocations": 13}}),
            budgets({"op/c": {"allocations": 12}}))
        self.assertTrue(report.failed)
        self.assertIn("over its budget", report.failures[0])


# spec: SWR-PERF-0004
class SeedBudgets(unittest.TestCase):
    def test_ten_percent_headroom_rounded_up(self):
        seeded = seed_budgets.seed(
            results({"op/c": {"instructions": 1001, "allocations": 10, "allocated_bytes": 0,
                              "retained_bytes": 100, "wall_ns": 5.5, "binary_bytes": 9}}),
            Fraction("0.10"))
        self.assertEqual(seeded["budgets"]["op/c"], {
            "instructions": 1102, "allocations": 11, "allocated_bytes": 0, "retained_bytes": 110,
        })

    def test_a_whole_result_is_not_rounded_past(self):
        self.assertEqual(seed_budgets.budget_for(1000, Fraction("0.10")), 1100)

    def test_records_the_toolchain_it_was_seeded_with(self):
        seeded = seed_budgets.seed(results({"op/c": {"allocations": 1}}), Fraction("0.10"))
        self.assertEqual(seeded["toolchain"]["arch"], "x86_64")
        self.assertEqual(seeded["toolchain"]["compiler_id"], "gcc-14")

    def test_the_file_is_sorted_one_measure_per_line(self):
        text = seed_budgets.render(seed_budgets.seed(
            results({"b/c": {"allocations": 1, "instructions": 2}, "a/c": {"allocations": 3}}),
            Fraction("0.10")))
        lines = text.splitlines()
        self.assertLess(text.index('"a/c"'), text.index('"b/c"'))
        self.assertIn('      "allocations": 2,', lines)
        self.assertEqual(json.loads(text)["budgets"]["b/c"]["instructions"], 3)

    def test_seeded_budgets_pass_the_run_they_came_from(self):
        head = {"op/c": {"instructions": 12345, "allocations": 7, "retained_bytes": 99}}
        seeded = seed_budgets.seed(results(head), Fraction("0.10"))
        self.assertFalse(compare.compare(None, results(head), seeded).failed)


# spec: SWR-PERF-0005
class SummaryTable(unittest.TestCase):
    def render(self, base, head, limits=None):
        report = gate(base, head, limits)
        return compare.render(report, results(base, sha="main"), results(head), [])

    def test_every_measurement_has_main_pr_delta_budget_and_verdict(self):
        text = self.render({"op/c": {"instructions": 1000, "wall_ns": 10.0}},
                           {"op/c": {"instructions": 1010, "wall_ns": 12.5}},
                           budgets({"op/c": {"instructions": 1100}}))
        self.assertIn("| id | measure | main | PR | delta | budget | verdict |", text)
        self.assertIn("| `op/c` | instructions | 1,000 | 1,010 | +1.00% | 1,100 | ok |", text)
        self.assertIn("| `op/c` | wall_ns | 10 | 12.5 | +25.00% | - | - |", text)

    def test_failures_are_listed_first(self):
        text = self.render(
            {"a/c": {"allocations": 1}, "z/c": {"allocations": 1}},
            {"a/c": {"allocations": 1}, "z/c": {"allocations": 2}})
        self.assertLess(text.index("### Failures"), text.index("| id |"))
        self.assertLess(text.index("| `z/c` | allocations"), text.index("| `a/c` | allocations"))

    def test_carries_the_marker_the_comment_is_found_by(self):
        text = self.render({"op/c": {"allocations": 1}}, {"op/c": {"allocations": 1}})
        self.assertTrue(text.startswith(compare.MARKER))

    def test_the_command_line_exits_one_on_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "base.json").write_text(json.dumps(results({"op/c": {"allocations": 1}})))
            (root / "head.json").write_text(json.dumps(results({"op/c": {"allocations": 2}})))
            (root / "budgets.json").write_text(json.dumps(budgets({"op/c": {"allocations": 9}})))
            argv = ["--base", str(root / "base.json"), "--head", str(root / "head.json"),
                    "--budgets", str(root / "budgets.json"), "--summary", str(root / "s.md")]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(argv), 1)
            self.assertIn("FAIL", (root / "s.md").read_text())
            (root / "head.json").write_text(json.dumps(results({"op/c": {"allocations": 1}})))
            with redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(argv), 0)

    def test_an_absent_base_file_means_budgets_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "head.json").write_text(json.dumps(results({"op/c": {"allocations": 1}})))
            (root / "budgets.json").write_text(json.dumps(budgets({"op/c": {"allocations": 1}})))
            out = io.StringIO()
            with redirect_stdout(out):
                code = compare.main(["--base", str(root / "missing.json"),
                                     "--head", str(root / "head.json"),
                                     "--budgets", str(root / "budgets.json")])
            self.assertEqual(code, 0)
            self.assertIn("Only the budgets gate", out.getvalue())

    def test_a_failed_probe_names_its_id_mode_and_message(self):
        said = "perf_probe: decode_large/rapidjson retained 3, 1 and 2 bytes across identical runs"

        def failing(cmd, **_):
            raise subprocess.CalledProcessError(1, cmd, output="", stderr=said + "\n")

        original = measure.run
        measure.run = failing
        try:
            with self.assertRaises(measure.MeasureFailure) as caught:
                measure.probe_json(Path("perf_probe"), "retained", "decode_large/rapidjson")
        finally:
            measure.run = original
        self.assertEqual(caught.exception.as_json(),
                         {"id": "decode_large/rapidjson", "mode": "perf_probe retained",
                          "message": said})

    def test_a_measurement_that_failed_is_reported_and_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "failure.json").write_text(json.dumps(
                {"id": "decode_large/rapidjson", "mode": "perf_probe retained",
                 "message": "perf_probe: retained 3, 1 and 2 bytes <across> runs"}))
            argv = ["--measure-failure", str(root / "failure.json"),
                    "--summary", str(root / "s.md")]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(argv), 1)
            text = (root / "s.md").read_text()
        self.assertTrue(text.startswith(compare.MARKER))
        self.assertIn("could not be measured", text)
        self.assertIn("`decode_large/rapidjson` failed in `perf_probe retained`", text)
        self.assertIn("retained 3, 1 and 2 bytes &lt;across&gt; runs", text)

    def test_a_build_that_stopped_before_measuring_is_reported_and_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = ["--measure-failure", str(root / "never-written.json"),
                    "--summary", str(root / "s.md")]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(argv), 1)
            text = (root / "s.md").read_text()
        self.assertTrue(text.startswith(compare.MARKER))
        self.assertIn("The probe did not build or did not run", text)


# spec: SWR-PERF-0006
class BinarySizeWarning(unittest.TestCase):
    def test_growth_over_five_percent_warns_and_passes(self):
        report = gate({"consumer/c": {"binary_bytes": 100000}},
                      {"consumer/c": {"binary_bytes": 105001}})
        self.assertFalse(report.failed)
        self.assertEqual(len(report.warnings), 1)
        self.assertIn("SWR-PERF-0006", report.warnings[0])

    def test_exactly_five_percent_is_quiet(self):
        report = gate({"consumer/c": {"binary_bytes": 100000}},
                      {"consumer/c": {"binary_bytes": 105000}})
        self.assertFalse(report.failed)
        self.assertEqual(report.warnings, [])

    def test_binary_size_needs_no_budget(self):
        report = gate(None, {"consumer/c": {"binary_bytes": 100000}}, budgets({}))
        self.assertFalse(report.failed)


# spec: SWR-PERF-0007
class History(unittest.TestCase):
    def run_history(self, data_dir: Path, sha: str, instructions: int) -> None:
        results_file = data_dir.parent / f"{sha}.json"
        results_file.write_text(json.dumps(
            results({"op/c": {"instructions": instructions, "allocations": 3}}, sha=sha)))
        with redirect_stderr(io.StringIO()):
            history.main(["--results", str(results_file), "--data-dir", str(data_dir)])

    def test_appends_one_line_per_run_and_regenerates_the_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "bench-data"
            self.run_history(data, "aaaa", 1000)
            self.run_history(data, "bbbb", 1100)
            lines = (data / "history.jsonl").read_text().splitlines()
            self.assertEqual([json.loads(line)["git_sha"] for line in lines], ["aaaa", "bbbb"])
            report = (data / "README.md").read_text()
            self.assertIn("## `op/c`", report)
            self.assertIn("| `aaaa` | 2026-09-24 | 1,000 | 3 |", report)
            self.assertIn("instructions up +10.0%", report)
            self.assertIn("allocations flat", report)

    def test_the_report_keeps_the_last_thirty_runs(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "bench-data"
            for i in range(35):
                self.run_history(data, f"run{i:02d}", 1000 + i)
            report = (data / "README.md").read_text()
            self.assertNotIn("`run04`", report)
            self.assertIn("`run05`", report)
            self.assertIn("`run34`", report)
            self.assertEqual(len((data / "history.jsonl").read_text().splitlines()), 35)


if __name__ == "__main__":
    unittest.main()
