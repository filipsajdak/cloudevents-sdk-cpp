#!/usr/bin/env python3
"""Compare a pull request's measurements with main's and with the budgets.

    compare.py --base base.json --head head.json \\
               --budgets bench/budgets.json --summary summary.md

Exit 1 when any gate fails, 0 otherwise. The summary is Markdown for the job
summary and the pull request comment, failures first.

The gates (ADR-0011):

    instructions               fail above +2% over main      SWR-PERF-0001
    allocations, bytes         fail on any increase           SWR-PERF-0002
    retained_bytes             fail above +1% over main      SWR-PERF-0003
    every gated measure        fail above its budget         SWR-PERF-0004
    binary_bytes               warn above +5% over main      SWR-PERF-0006
    wall_ns, cpu_ns            reported only

A pull request that raises a budget accepts that cost: when `--base-budgets`
(main's bench/budgets.json) shows the budget went up, a growth over main in
that id and measure is reported as accepted instead of failing. The budget
itself still gates.

A missing or unreadable base (the first run, or a base that does not build)
leaves the budgets as the only gate, and the summary says so. A gated measure
with no budget fails: every operation the job measures needs a limit someone
chose.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from fractions import Fraction
from pathlib import Path

from seed_budgets import budget_for

MARKER = "<!-- ce-perf-report -->"

# measure -> (requirement, allowed growth over main in percent)
GATES = {
    "instructions": ("SWR-PERF-0001", 2),
    "allocations": ("SWR-PERF-0002", 0),
    "allocated_bytes": ("SWR-PERF-0002", 0),
    "retained_bytes": ("SWR-PERF-0003", 1),
}
WARNINGS = {
    "binary_bytes": ("SWR-PERF-0006", 5),
}
REPORTED = ("wall_ns", "cpu_ns")
BUDGET_REQUIREMENT = "SWR-PERF-0004"
SEED_HEADROOM = Fraction(1, 10)

MEASURE_ORDER = [*GATES, *WARNINGS, *REPORTED]

FAIL, WARN, ACCEPTED, PASS, INFO = "fail", "warn", "accepted", "pass", "info"
VERDICT_ORDER = {FAIL: 0, WARN: 1, ACCEPTED: 2, PASS: 3, INFO: 4}
VERDICT_LABEL = {FAIL: "FAIL", WARN: "warn", ACCEPTED: "accepted", PASS: "ok", INFO: "-"}


@dataclass
class Row:
    op_id: str
    measure: str
    base: float | None
    head: float | None
    budget: float | None
    verdict: str
    reasons: list[str] = field(default_factory=list)

    @property
    def delta(self) -> float | None:
        if self.base is None or self.head is None or self.base == 0:
            return None
        return (self.head - self.base) * 100.0 / self.base


@dataclass
class Report:
    rows: list[Row]
    notes: list[str]
    failures: list[str]
    warnings: list[str]
    accepted: list[str] = field(default_factory=list)

    @property
    def failed(self) -> bool:
        return bool(self.failures)


def exceeds(head: float, base: float, percent: int) -> bool:
    """True when head is more than `percent` above base. Integer arithmetic,
    so a result exactly on the threshold passes."""
    return head * 100 > base * (100 + percent)


def toolchain_mismatch(budgets: dict, head: dict) -> str | None:
    seeded = budgets.get("toolchain") or {}
    measured = head.get("toolchain") or {}
    for key in ("arch", "compiler_id"):
        if seeded.get(key) and seeded.get(key) != measured.get(key):
            return (
                f"`bench/budgets.json` was seeded with {key} `{seeded.get(key)}`, and this run "
                f"measured with `{measured.get(key)}`. Instruction counts differ between the two, "
                "so the budgets cannot be applied. Re-seed them from this toolchain: run the "
                "perf workflow by hand with `seed` set, and commit the budgets.json it uploads."
            )
    return None


def raised(op_id: str, measure: str, budget: float | None, base_limits: dict | None) -> bool:
    """Whether this pull request raised (or first set) the budget for a measure
    that main already had budgets for."""
    if budget is None or base_limits is None:
        return False
    before = (base_limits.get(op_id) or {}).get(measure)
    return before is None or budget > before


def compare(base: dict | None, head: dict, budgets: dict | None,
            base_budgets: dict | None = None) -> Report:
    notes: list[str] = []
    failures: list[str] = []
    warnings: list[str] = []
    accepted: list[str] = []
    rows: list[Row] = []
    base_limits = (base_budgets or {}).get("budgets") if base_budgets is not None else None

    base_measurements = (base or {}).get("measurements") or {}
    head_measurements = head.get("measurements") or {}
    limits = (budgets or {}).get("budgets") or {}

    if base is None:
        notes.append(
            "No measurements of main: this is the first run, or main does not build with "
            "this harness. Only the budgets gate this pull request."
        )

    mismatch = toolchain_mismatch(budgets or {}, head)
    if mismatch:
        failures.append(f"{mismatch} ({BUDGET_REQUIREMENT})")
        limits = {}

    for op_id in sorted(head_measurements):
        measured = head_measurements[op_id]
        before = base_measurements.get(op_id)
        if base is not None and before is None:
            notes.append(f"`{op_id}` is new in this pull request; main has no measurement of it.")
        for measure in sorted(measured, key=lambda m: (MEASURE_ORDER.index(m)
                                                       if m in MEASURE_ORDER else 99, m)):
            value = measured[measure]
            prior = (before or {}).get(measure)
            budget = (limits.get(op_id) or {}).get(measure)
            row = Row(op_id, measure, prior, value, budget, INFO)

            if measure in GATES:
                requirement, percent = GATES[measure]
                row.verdict = PASS
                if prior is not None and exceeds(value, prior, percent):
                    allowance = "any increase" if percent == 0 else f"+{percent}%"
                    growth = (f"`{op_id}` {measure}: {fmt(prior)} on main, {fmt(value)} here "
                              f"({fmt_delta(row.delta)})")
                    if raised(op_id, measure, budget, base_limits):
                        row.verdict = ACCEPTED
                        accepted.append(f"{growth}; this pull request raised its budget "
                                        f"to {fmt(budget)}, which accepts the cost")
                    else:
                        row.verdict = FAIL
                        row.reasons.append(f"{growth}; the limit is {allowance} ({requirement})")
                if budget is None and not mismatch:
                    row.verdict = FAIL
                    row.reasons.append(
                        f"`{op_id}` {measure} has no budget. Add one to `bench/budgets.json` "
                        f"in this pull request: the measurement plus 10% headroom is "
                        f"{budget_for(value, SEED_HEADROOM)} ({BUDGET_REQUIREMENT})"
                    )
                elif budget is not None and value > budget:
                    row.verdict = FAIL
                    row.reasons.append(
                        f"`{op_id}` {measure}: {fmt(value)} is over its budget of {fmt(budget)}. "
                        f"If the cost is deliberate, raise the budget in this pull request and "
                        f"say why in the commit ({BUDGET_REQUIREMENT})"
                    )
                failures.extend(row.reasons)
            elif measure in WARNINGS:
                requirement, percent = WARNINGS[measure]
                row.verdict = PASS
                if prior is not None and exceeds(value, prior, percent):
                    row.verdict = WARN
                    row.reasons.append(
                        f"`{op_id}` {measure}: {fmt(prior)} on main, {fmt(value)} here "
                        f"({fmt_delta(row.delta)}), above +{percent}% ({requirement}). "
                        "A warning only: binary size depends on the compiler as much as the change."
                    )
                    warnings.extend(row.reasons)
            rows.append(row)

    for op_id in sorted(limits):
        if op_id not in head_measurements:
            warnings.append(
                f"`{op_id}` has a budget but was not measured; remove it from "
                "`bench/budgets.json` if the operation is gone."
            )

    rows.sort(key=lambda r: (VERDICT_ORDER[r.verdict], r.op_id, MEASURE_ORDER.index(r.measure)
                             if r.measure in MEASURE_ORDER else 99))
    return Report(rows=rows, notes=notes, failures=failures, warnings=warnings,
                  accepted=accepted)


def fmt(value: float | None) -> str:
    if value is None:
        return "-"
    if isinstance(value, float) and not value.is_integer():
        return f"{value:,.1f}"
    return f"{int(value):,}"


def fmt_delta(delta: float | None) -> str:
    if delta is None:
        return "-"
    if delta == 0:
        return "="
    return f"{delta:+.2f}%"


def table(rows: list[Row]) -> list[str]:
    lines = [
        "| id | measure | main | PR | delta | budget | verdict |",
        "|---|---|--:|--:|--:|--:|---|",
    ]
    for row in rows:
        lines.append(
            f"| `{row.op_id}` | {row.measure} | {fmt(row.base)} | {fmt(row.head)} | "
            f"{fmt_delta(row.delta)} | {fmt(row.budget)} | {VERDICT_LABEL[row.verdict]} |"
        )
    return lines


def render(report: Report, base: dict | None, head: dict, extra_notes: list[str]) -> str:
    base_sha = ((base or {}).get("git_sha") or "none")[:12]
    head_sha = (head.get("git_sha") or "unknown")[:12]
    toolchain = head.get("toolchain") or {}
    if report.failed:
        verdict = f"**{len(report.failures)} failure(s)**"
    elif report.warnings:
        verdict = f"**passed with {len(report.warnings)} warning(s)**"
    else:
        verdict = "**passed**"

    lines = [
        MARKER,
        "## Performance",
        "",
        f"{verdict}: `{head_sha}` measured against main `{base_sha}` and `bench/budgets.json`, "
        f"on {toolchain.get('arch', '?')} with {toolchain.get('compiler', '?')}.",
        "",
    ]
    for note in [*extra_notes, *report.notes]:
        lines.append(f"> {note}")
        lines.append(">")
    if lines[-1] == ">":
        lines[-1] = ""

    if report.failures:
        lines += ["### Failures", ""]
        lines += [f"- {failure}" for failure in report.failures]
        lines.append("")
    if report.warnings:
        lines += ["### Warnings", ""]
        lines += [f"- {warning}" for warning in report.warnings]
        lines.append("")
    if report.accepted:
        lines += ["### Costs accepted by a raised budget", ""]
        lines += [f"- {cost}" for cost in report.accepted]
        lines.append("")

    flagged = [r for r in report.rows if r.verdict in (FAIL, WARN, ACCEPTED)]
    rest = [r for r in report.rows if r.verdict not in (FAIL, WARN, ACCEPTED)]
    if flagged:
        lines += table(flagged)
        lines.append("")
    lines += [
        f"<details><summary>{len(rest)} measurements within their limits</summary>",
        "",
        *table(rest),
        "",
        "</details>",
        "",
        "Instructions, allocations and retained bytes gate; binary size warns; "
        "wall and CPU time are reported only. See `docs/PERFORMANCE.md`.",
        "",
    ]
    return "\n".join(lines)


def load(path: Path | None) -> dict | None:
    if path is None or not str(path) or not path.is_file():
        return None
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", type=Path, help="main's results; absent or empty means none")
    parser.add_argument("--head", type=Path, required=True)
    parser.add_argument("--budgets", type=Path, required=True)
    parser.add_argument("--base-budgets", type=Path,
                        help="main's budgets.json; a budget this pull request raised accepts "
                             "the growth over main in that measure")
    parser.add_argument("--summary", type=Path, help="write the Markdown here as well as stdout")
    parser.add_argument("--note", action="append", default=[],
                        help="a line to show above the table, such as how main was built")
    args = parser.parse_args(argv)

    head = load(args.head)
    if head is None:
        print(f"compare: cannot read {args.head}", file=sys.stderr)
        return 2
    budgets = load(args.budgets)
    if budgets is None:
        args.note.append(f"`{args.budgets}` is missing or unreadable, so every gated "
                         "measure fails for want of a budget.")

    base = load(args.base)
    report = compare(base, head, budgets, load(args.base_budgets))
    markdown = render(report, base, head, args.note)
    if args.summary:
        args.summary.write_text(markdown)
    print(markdown)
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
