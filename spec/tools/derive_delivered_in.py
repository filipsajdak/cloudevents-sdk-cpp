#!/usr/bin/env python3
"""Derive `delivered_in` for requirements from the release tags (SWR-BUILD-0016).

A requirement is delivered by the FIRST release tag whose tree holds all of:

1. its current statement (whitespace-normalised) and current `version`, in the
   requirement's own file at that tag, so a requirement amended after a release
   is not counted as delivered by the older tag;
2. every `verified_by` target, resolved the way `check_references.py` resolves
   it: the test file exists and the named suite or case is declared in it (a file
   that declares no suite, such as a fuzz target, resolves by existing; a
   CMake file must contain the name, an `acc:`/`val:` id must have its
   acceptance page).

The first tag that merely contains the requirement's file is the wrong answer:
the spec is written before the code, so the file predates the release that
delivered it.

`satisfied_by` is deliberately not a condition. A header that moved or gained a
sibling after the release (a refactor that leaves the statement alone) would
otherwise push the tag to a release that delivered nothing new; measured on the
requirements already marked `implemented`, the files named by the current
`satisfied_by` made nine of them disagree with the tag a person recorded. A
test that resolves at a tag compiled against the code of that tag, so the tests
are the evidence of delivery.

Modes:
  (default)  print a table: uid, current status, derived tag or `none: <reason>`
  --write    for each `approved` requirement with a derived tag, set
             `status: implemented` and `delivered_in: <tag>` by editing the two
             lines in place; nothing else in the file changes
  --check    exit 1 if --write would change anything, listing the files

Tags are read with `git`; no tag is ever checked out. Standard library plus
pyyaml (through reqlib).
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

import check_references
import reqlib

EXIT_OK = 0
EXIT_DRIFT = 1

TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
STATUS_APPROVED = "approved"
STATUS_DELIVERED = "implemented"
ACCEPTANCE_PAGE = "docs/acceptance/{id}.md"
FRONTMATTER_FENCE = "---\n"
CLOSING_FENCE = "\n---\n"
STATUS_LINE_RE = re.compile(r"^status: approved[ \t]*$", re.MULTILINE)
DELIVERED_LINE_RE = re.compile(r"^delivered_in:.*$", re.MULTILINE)


class GitTree:
    """The files of one release tag, read through git and cached."""

    def __init__(self, repo: pathlib.Path, tag: str) -> None:
        self.repo = repo
        self.tag = tag
        listing = self._git("ls-tree", "-r", "--name-only", tag)
        self._paths = set(listing.splitlines()) if listing is not None else set()
        self._texts: dict[str, str | None] = {}

    def _git(self, *args: str) -> str | None:
        done = subprocess.run(
            ["git", "-C", str(self.repo), *args],
            capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
        )
        return done.stdout if done.returncode == 0 else None

    def exists(self, path: str) -> bool:
        return path in self._paths

    def read(self, path: str) -> str | None:
        if path not in self._paths:
            return None
        if path not in self._texts:
            self._texts[path] = self._git("show", f"{self.tag}:{path}")
        return self._texts[path]


def release_tags(repo: pathlib.Path) -> list[str]:
    """Release tags, oldest first by version number."""
    done = subprocess.run(
        ["git", "-C", str(repo), "tag", "--list"],
        capture_output=True, text=True, check=False,
    )
    tags = [t for t in done.stdout.split() if TAG_RE.match(t)]
    return sorted(tags, key=lambda t: tuple(int(n) for n in TAG_RE.match(t).groups()))


def normalise(statement: str) -> str:
    return " ".join(statement.split())


def unresolved_reference(ref: str, tree: GitTree) -> str | None:
    """Why `ref` does not resolve in `tree`, or None when it does."""
    prefix, _, rest = str(ref).partition(":")
    if prefix in ("code", "doc"):
        return None if tree.exists(rest) else f"{ref} absent"
    if prefix in ("acc", "val"):
        return None if tree.exists(ACCEPTANCE_PAGE.format(id=rest)) else f"{ref} absent"
    if prefix == "test":
        return unresolved_test(ref, rest, tree)
    return f"{ref} is not a known reference"


def unresolved_test(ref: str, rest: str, tree: GitTree) -> str | None:
    """`test:<path>::<name>` against the files of `tree`."""
    if "::" not in rest:
        return f"{ref} is not path-qualified"
    path, name = rest.split("::", 1)
    text = tree.read(path)
    if text is None:
        return f"{ref} file absent"
    if path.endswith("CMakeLists.txt"):
        known = {name} if name in text else set()
    else:
        # Like check_references: a file that declares no suite at all (a fuzz
        # target) is named by a free label, so its existence is the resolution.
        known = check_references.names_in_file(path, text) or {name}
    return None if name in known else f"{ref} name absent"


def unmet(req: reqlib.Requirement, rel: str, tree: GitTree) -> str | None:
    """The first reason `req` is not delivered by `tree`, or None when it is."""
    text = tree.read(rel)
    if text is None:
        return "requirement file absent"
    try:
        then = reqlib.parse_requirement_text(pathlib.Path(rel), text)
    except reqlib.FrontmatterError:
        return "requirement file unreadable"
    if then.meta.get("version") != req.meta.get("version"):
        return f"version {then.meta.get('version')}, now {req.meta.get('version')}"
    if normalise(then.body) != normalise(req.body):
        return "statement differs"
    verified = req.meta.get("verified_by") or []
    if not verified:
        return "no verified_by"
    for ref in verified:
        reason = unresolved_reference(ref, tree)
        if reason:
            return reason
    return None


def derive(req: reqlib.Requirement, rel: str, trees: list[GitTree]) -> tuple[str | None, str]:
    """(first delivering tag, "") or (None, why the newest tag does not deliver)."""
    reason = "no release tags"
    for tree in trees:
        found = unmet(req, rel, tree)
        if found is None:
            return tree.tag, ""
        reason = f"at {tree.tag}: {found}"
    return None, reason


def requirement_files(repo: pathlib.Path) -> list[pathlib.Path]:
    root = repo / reqlib.REQ_DIR.relative_to(reqlib.REPO_ROOT)
    return [p for level in reqlib.LEVEL_DIRS for p in sorted((root / level).glob("*.md"))]


def mark_delivered(text: str, tag: str) -> str:
    """Set `status: implemented` and `delivered_in: <tag>`, touching only those lines."""
    opening, front_and_rest = text[:len(FRONTMATTER_FENCE)], text[len(FRONTMATTER_FENCE):]
    front, closing, body = front_and_rest.partition(CLOSING_FENCE)
    status = f"status: {STATUS_DELIVERED}"
    delivered = f"delivered_in: {tag}"
    if DELIVERED_LINE_RE.search(front):
        front = STATUS_LINE_RE.sub(status, front, count=1)
        front = DELIVERED_LINE_RE.sub(delivered, front, count=1)
    else:
        front = STATUS_LINE_RE.sub(f"{status}\n{delivered}", front, count=1)
    return opening + front + closing + body


def derive_all(repo: pathlib.Path) -> list[tuple[pathlib.Path, reqlib.Requirement, str | None, str]]:
    trees = [GitTree(repo, tag) for tag in release_tags(repo)]
    rows = []
    for path in requirement_files(repo):
        req = reqlib.parse_requirement_file(path)
        tag, reason = derive(req, str(path.relative_to(repo)), trees)
        rows.append((path, req, tag, reason))
    return rows


def render_table(rows) -> str:
    cells = [
        (req.uid, str(req.meta.get("status")), tag if tag else f"none: {reason}")
        for _, req, tag, reason in rows
    ]
    widths = [max(len(c[i]) for c in cells) for i in range(len(cells[0]))] if cells else []
    return "\n".join("  ".join(c.ljust(w) for c, w in zip(row, widths)).rstrip() for row in cells)


def pending_edits(rows) -> list[tuple[pathlib.Path, str]]:
    """(path, new text) for each approved requirement a tag delivers."""
    edits = []
    for path, req, tag, _ in rows:
        if tag and req.meta.get("status") == STATUS_APPROVED:
            edits.append((path, mark_delivered(path.read_text(encoding="utf-8"), tag)))
    return edits


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--repo", type=pathlib.Path, default=reqlib.REPO_ROOT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="edit approved requirements a tag delivers")
    mode.add_argument("--check", action="store_true", help="exit 1 if --write would change anything")
    args = parser.parse_args(argv)

    rows = derive_all(args.repo)
    print(render_table(rows))
    edits = pending_edits(rows)
    if args.write:
        for path, text in edits:
            path.write_text(text, encoding="utf-8")
        print(f"wrote {len(edits)} requirement file(s)")
    if args.check and edits:
        print(f"FAIL: {len(edits)} requirement(s) are approved but delivered by a tag:")
        for path, _ in edits:
            print(f"  - {path.relative_to(args.repo)}")
        return EXIT_DRIFT
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
