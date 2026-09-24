#!/usr/bin/env python3
"""Propose bench/budgets.json from one set of measurements.

    seed_budgets.py --results results.json --headroom 0.10 --out bench/budgets.json

Every gated measure (instructions, allocations, allocated bytes, retained
bytes) gets a budget of the measurement plus the headroom, rounded up. The
file records the toolchain it was seeded with, because an instruction count is
only comparable with one from the same architecture and compiler; compare.py
refuses budgets from another.

Seed from the CI toolchain - the perf workflow's manual run with `seed` set
uploads this file - never from a developer machine. The output is sorted, one
line per id and measure, so a change to a budget is a one-line diff.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import sys
from fractions import Fraction
from pathlib import Path

GATED = ("instructions", "allocations", "allocated_bytes", "retained_bytes")


def budget_for(value: int | float, headroom: Fraction) -> int:
    """value * (1 + headroom), rounded up, in exact arithmetic so a whole
    result is not pushed up by a floating-point error."""
    scaled = Fraction(value) * (1 + headroom)
    whole = scaled.numerator // scaled.denominator
    return whole if whole == scaled else whole + 1


def seed(results: dict, headroom: Fraction) -> dict:
    budgets: dict[str, dict[str, int]] = {}
    for op_id, measured in sorted((results.get("measurements") or {}).items()):
        limits = {m: budget_for(measured[m], headroom) for m in GATED if m in measured}
        if limits:
            budgets[op_id] = limits
    toolchain = results.get("toolchain") or {}
    return {
        "headroom": float(headroom),
        "seeded_from": results.get("git_sha", "unknown"),
        "toolchain": {key: toolchain.get(key, "unknown")
                      for key in ("arch", "compiler", "compiler_id", "valgrind")},
        "budgets": budgets,
    }


def render(document: dict) -> str:
    return json.dumps(document, indent=2, sort_keys=True) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--headroom", default="0.10",
                        help="fraction added to each measurement (default 0.10)")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    results = json.loads(args.results.read_text())
    headroom = Fraction(args.headroom)
    if headroom < 0:
        print("seed_budgets: headroom cannot be negative", file=sys.stderr)
        return 2
    args.out.write_text(render(seed(results, headroom)))
    print(f"seed_budgets: wrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
