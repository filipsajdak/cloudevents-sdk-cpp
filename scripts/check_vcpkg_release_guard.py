#!/usr/bin/env python3
"""Decide whether the vcpkg released-source job has anything to prove yet.

SWR-ADOPT-0009. The port names a release by the version in vcpkg.json and by the
SHA-512 of that release's archive in portfile.cmake. The hash can only be
committed after the tag exists, so the release pull request carries a version
whose tag is missing, and the commits between the tag and the post-tag hash
commit carry a hash that is still the previous release's. A job that installed
the port then would be red for a reason that is not a defect. This script says
which of the three states the tree is in:

    tag missing          skip: the release the port names has not been tagged
    hash not updated     skip: the committed SHA-512 is the one held by the
                         port at the tag v<version> or at the previous tag
    otherwise            run

The tag's own portfile holds the previous release's hash by design (it is
committed before the archive exists), so comparing with it detects "tagged,
hash not yet committed". The previous tag's portfile is compared as well, which
covers a hash that was not moved across two releases.

    python3 scripts/check_vcpkg_release_guard.py [--root DIR] [--remote NAME]
                                                 [--github-output FILE]

Prints `run=true` or `run=false` and `reason=...`; with --github-output it
appends the same two lines to that file, and on a skip prints a `::notice::`
workflow command. Exit 0 for either verdict, 2 when it could not decide: a
file is unreadable, the remote cannot be listed, or a tag on the remote is
missing from the local clone (a shallow checkout), because deciding "run" on
missing evidence would be a verdict the script did not reach.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import re
import subprocess
import sys
from collections.abc import Mapping

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

VCPKG_MANIFEST = pathlib.Path("packaging/vcpkg/ports/cloudevents-cpp/vcpkg.json")
PORTFILE = pathlib.Path("packaging/vcpkg/ports/cloudevents-cpp/portfile.cmake")

EXIT_DECIDED = 0
EXIT_UNDECIDED = 2

# A SHA-512 digest is 64 bytes, written as two hex digits each.
SHA512_HEX_DIGITS = 128
DEFAULT_REMOTE = "origin"

SHA512_LINE = re.compile(rf"^\s*SHA512\s+([0-9a-f]{{{SHA512_HEX_DIGITS}}})\s*$", re.MULTILINE)
RELEASE_TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
LS_REMOTE_TAG = re.compile(r"^[0-9a-f]+\trefs/tags/(.+?)(\^\{\})?$")

Version = tuple[int, int, int]


class Undecided(Exception):
    """The evidence the verdict needs could not be read."""


@dataclasses.dataclass(frozen=True)
class Verdict:
    run: bool
    reason: str


def manifest_version(manifest_text: str) -> str:
    try:
        version = json.loads(manifest_text).get("version")
    except json.JSONDecodeError as error:
        raise Undecided(f"{VCPKG_MANIFEST}: not JSON: {error}") from error
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise Undecided(f"{VCPKG_MANIFEST}: no x.y.z version, found {version!r}")
    return version


def portfile_sha512(portfile_text: str) -> str | None:
    """The hash the portfile passes to vcpkg_from_github, or None if it names none."""
    found = SHA512_LINE.search(portfile_text)
    return found.group(1) if found else None


def tag_for(version: str) -> str:
    return f"v{version}"


def _release_key(tag: str) -> Version | None:
    found = RELEASE_TAG.match(tag)
    return (int(found.group(1)), int(found.group(2)), int(found.group(3))) if found else None


def previous_tag(tags: list[str], version: str) -> str | None:
    """The highest release tag strictly below v<version>, or None."""
    current = _release_key(tag_for(version))
    below = [(key, tag) for tag in tags if (key := _release_key(tag)) and current and key < current]
    return max(below)[1] if below else None


def parse_ls_remote(output: str) -> list[str]:
    """Tag names in `git ls-remote --tags` output, annotated tags once."""
    names: dict[str, None] = {}
    for line in output.splitlines():
        found = LS_REMOTE_TAG.match(line)
        if found:
            names[found.group(1)] = None
    return list(names)


def decide(version: str, committed_sha512: str | None, remote_tags: list[str],
           held_sha512: Mapping[str, str | None]) -> Verdict:
    """The verdict from facts already read.

    held_sha512 maps a tag to the hash the port holds at that tag, or None when
    the port has none there (the tag predates the port).
    """
    tag = tag_for(version)
    if tag not in remote_tags:
        return Verdict(False, f"the tag {tag} is not on the remote: the release the port names is not tagged yet")
    if committed_sha512 is None:
        return Verdict(False, f"{PORTFILE} names no SHA512, so there is no archive hash to prove")
    previous = previous_tag(remote_tags, version)
    for name in (tag, previous):
        if name is not None and held_sha512.get(name) == committed_sha512:
            return Verdict(False, f"the committed SHA512 is the one held at {name}: "
                                  f"the post-tag commit has not moved it to the archive of {tag}")
    return Verdict(True, f"{tag} is on the remote and the committed SHA512 is new")


def _git(root: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True, check=False)


def remote_tags(root: pathlib.Path, remote: str) -> list[str]:
    listed = _git(root, "ls-remote", "--tags", remote)
    if listed.returncode != 0:
        raise Undecided(f"git ls-remote --tags {remote} failed: {listed.stderr.strip()}")
    return parse_ls_remote(listed.stdout)


def portfile_sha512_at(root: pathlib.Path, tag: str) -> str | None:
    """The hash the port holds at a tag; None when the port does not exist there."""
    if _git(root, "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}").returncode != 0:
        raise Undecided(f"the tag {tag} is on the remote but not in this clone: fetch the tags first")
    shown = _git(root, "show", f"refs/tags/{tag}:{PORTFILE.as_posix()}")
    return portfile_sha512(shown.stdout) if shown.returncode == 0 else None


def read_verdict(root: pathlib.Path, remote: str) -> Verdict:
    try:
        version = manifest_version((root / VCPKG_MANIFEST).read_text(encoding="utf-8"))
        committed = portfile_sha512((root / PORTFILE).read_text(encoding="utf-8"))
    except OSError as error:
        raise Undecided(f"cannot read the port: {error}") from error
    tags = remote_tags(root, remote)
    held: dict[str, str | None] = {}
    if tag_for(version) in tags:
        for name in (tag_for(version), previous_tag(tags, version)):
            if name is not None:
                held[name] = portfile_sha512_at(root, name)
    return decide(version, committed, tags, held)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=REPO_ROOT,
                        help="repository root (default: this checkout)")
    parser.add_argument("--remote", default=DEFAULT_REMOTE, help=f"remote to list tags on (default: {DEFAULT_REMOTE})")
    parser.add_argument("--github-output", type=pathlib.Path, default=None,
                        help="file to append run= and reason= lines to ($GITHUB_OUTPUT)")
    arguments = parser.parse_args(argv)

    try:
        verdict = read_verdict(arguments.root, arguments.remote)
    except Undecided as error:
        print(f"check_vcpkg_release_guard: {error}", file=sys.stderr)
        return EXIT_UNDECIDED

    lines = [f"run={'true' if verdict.run else 'false'}", f"reason={verdict.reason}"]
    print("\n".join(lines))
    if not verdict.run:
        print(f"::notice title=vcpkg released-source job skipped::{verdict.reason}")
    if arguments.github_output is not None:
        with arguments.github_output.open("a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    return EXIT_DECIDED


if __name__ == "__main__":
    sys.exit(main())
