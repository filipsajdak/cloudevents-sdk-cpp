#!/usr/bin/env python3
"""Fold the Mull reports of several suites into one verification line.

    mutation_summary.py [--new-tests N] [--equivalent E] [--arid A] LOG...

Each LOG is the output of `mull-runner --reporters IDE --ide-reporter-show-killed`
for one suite. Mull judges a mutant against the one executable it ran, but this
SDK is header-only and every suite includes the headers, so a mutant one suite
misses is often killed by the suite written for it. A mutant is therefore killed
when any suite killed it, and survives only when every suite that ran it missed.

Prints the surviving mutants, then the line a pull request ends its
Verification with:

    Mutation: Mull, changed lines: K killed, S survived (n new tests, e equivalent, a arid)

Exit 0 always: mutation is advice, not a merge gate.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# <file>:<line>:<column>: warning: Killed|Survived: <description> [<mutator>]
MUTANT_RE = re.compile(
    r"^(?P<where>\S.*?:\d+:\d+): warning: (?P<verdict>Killed|Survived): "
    r"(?P<what>.*) \[(?P<mutator>\w+)\]$"
)


def parse(text: str) -> dict[tuple[str, str, str], str]:
    """Verdict per mutant: its location, description and mutator."""
    verdicts: dict[tuple[str, str, str], str] = {}
    for line in text.splitlines():
        found = MUTANT_RE.match(line)
        if found:
            key = (found["where"], found["what"], found["mutator"])
            verdicts[key] = found["verdict"]
    return verdicts


def merge(reports: list[dict[tuple[str, str, str], str]]) -> dict[tuple[str, str, str], str]:
    merged: dict[tuple[str, str, str], str] = {}
    for report in reports:
        for key, verdict in report.items():
            if merged.get(key) != "Killed":
                merged[key] = verdict
    return merged


def summarise(merged: dict[tuple[str, str, str], str], new_tests: int, equivalent: int, arid: int) -> str:
    killed = sum(1 for verdict in merged.values() if verdict == "Killed")
    survived = len(merged) - killed
    if not merged:
        return "Mutation: none, because no mutable line in the changed lines was reached by a suite"
    return (
        f"Mutation: Mull, changed lines: {killed} killed, {survived} survived "
        f"({new_tests} new tests, {equivalent} equivalent, {arid} arid)"
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--new-tests", type=int, default=0)
    parser.add_argument("--equivalent", type=int, default=0)
    parser.add_argument("--arid", type=int, default=0)
    args = parser.parse_args(argv)

    merged = merge([parse(log.read_text(errors="replace")) for log in args.logs])
    for (where, what, mutator), verdict in sorted(merged.items()):
        if verdict == "Survived":
            print(f"survived: {where}: {what} [{mutator}]")
    print(summarise(merged, args.new_tests, args.equivalent, args.arid))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
