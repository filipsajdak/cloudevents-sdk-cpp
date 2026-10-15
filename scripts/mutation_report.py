#!/usr/bin/env python3
"""Render the mutation job's report for the job summary and the pull request comment.

    mutation_report.py --status N [--base-ref REF] [--run-url URL] LOG

LOG is the standard output of `scripts/mutation.sh <base-ref>` and N its exit
status. The report opens with a hidden marker, so the job can find its own comment
again and update it in place, then the outcome, the `Mutation:` verification line,
and the surviving mutants. Mutation is advice, never a merge gate (SWR-BUILD-0014),
so the report says so on every outcome.

Exit 0 always.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# The first line of the report; the job finds its comment by it.
MARKER = "<!-- ce-mutation-report -->"

# mutation.sh's exit codes beyond usage (2) and a missing tool (1).
EXIT_OK = 0
EXIT_NO_MUTANTS = 3
EXIT_NO_CONFIG = 4

# A comment longer than this is unreadable; the rest stays in the job log.
MAX_LISTED_SURVIVORS = 50
# Lines of the log quoted when the tool failed, so the comment says why.
FAILURE_TAIL_LINES = 25

SURVIVED_PREFIX = "survived: "
HEADER_ROOT = "/include/cloudevents/"
VERDICT_PREFIX = "Mutation:"
# What mutation.sh prints when the change has no C++ lines under include/.
NOTHING_TO_MUTATE = "Mutation: none, because the change has no C++ lines"

ADVISORY_NOTE = "Mutation testing is advice: a surviving mutant never fails this pull request."


def repo_relative(entry: str) -> str:
    """Drop the runner's checkout prefix: the reader knows which repository this is."""
    start = entry.find(HEADER_ROOT)
    return entry[start + 1:] if start > 0 else entry


def survivors(lines: list[str]) -> list[str]:
    return [repo_relative(line[len(SURVIVED_PREFIX):]) for line in lines if line.startswith(SURVIVED_PREFIX)]


def verdict_line(lines: list[str]) -> str | None:
    found = [line for line in lines if line.startswith(VERDICT_PREFIX)]
    return found[-1] if found else None


def headline(status: int, verdict: str | None) -> str:
    if status == EXIT_OK and verdict is not None and verdict.startswith(NOTHING_TO_MUTATE):
        return "Nothing to mutate: this pull request changes no C++ line under `include/`."
    if status == EXIT_OK:
        return "Mull ran on the lines this pull request changes."
    if status == EXIT_NO_MUTANTS:
        return (
            "This pull request changes lines under `include/`, but Mull ran no mutant on them: "
            "they hold no operator Mull mutates, or no suite reaches them."
        )
    if status == EXIT_NO_CONFIG:
        return "Mull did not read its configuration or could not resolve the base, so nothing was mutated. This job is broken, not the pull request."
    return f"`scripts/mutation.sh` stopped with status {status} before it reached a verdict. This job is broken, not necessarily the pull request."


def survivor_section(listed: list[str]) -> list[str]:
    if not listed:
        return []
    shown = listed[:MAX_LISTED_SURVIVORS]
    body = [f"<details><summary>{len(listed)} surviving mutant(s)</summary>", ""]
    body += [f"- `{entry}`" for entry in shown]
    if len(listed) > len(shown):
        body.append(f"- and {len(listed) - len(shown)} more in the job log")
    body += ["", "</details>"]
    return body


def failure_section(lines: list[str]) -> list[str]:
    tail = lines[-FAILURE_TAIL_LINES:]
    return ["<details><summary>end of the log</summary>", "", "```text", *tail, "```", "", "</details>"]


def render(status: int, text: str, base_ref: str = "", run_url: str = "") -> str:
    lines = text.splitlines()
    verdict = verdict_line(lines)
    parts = [MARKER, "### Mutation testing (advisory)", "", headline(status, verdict), ""]
    if verdict is not None:
        parts += ["```text", verdict, "```", ""]
    parts += survivor_section(survivors(lines))
    if status not in (EXIT_OK, EXIT_NO_MUTANTS):
        parts += failure_section(lines)
    context = [f"base `{base_ref}`" if base_ref else "", f"[job log]({run_url})" if run_url else ""]
    parts += ["", " | ".join(filter(None, [ADVISORY_NOTE, *context]))]
    return "\n".join(parts) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", type=Path)
    parser.add_argument("--status", type=int, required=True)
    parser.add_argument("--base-ref", default="")
    parser.add_argument("--run-url", default="")
    args = parser.parse_args(argv)
    text = args.log.read_text(errors="replace") if args.log.exists() else ""
    sys.stdout.write(render(args.status, text, args.base_ref, args.run_url))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
