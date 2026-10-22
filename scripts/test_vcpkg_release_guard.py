"""Suites for scripts/check_vcpkg_release_guard.py (SWR-ADOPT-0009).

Registered with CTest as `vcpkg_release_guard_tools`; standard-library Python
only (git is needed by the GitRepository suite):

    python3 -m unittest discover -s scripts -p 'test_vcpkg_release_*.py'

Decide and Parse work on facts passed in. GitRepository builds a throwaway
repository whose tags stand for releases, so the reading of the remote and of the
port at a tag is exercised against real git and never against this repository.
"""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_vcpkg_release_guard as guard  # noqa: E402

OLD_HASH = "a" * guard.SHA512_HEX_DIGITS
NEW_HASH = "b" * guard.SHA512_HEX_DIGITS
OLDER_HASH = "c" * guard.SHA512_HEX_DIGITS
VERSION = "1.2.3"
CURRENT_TAG = "v1.2.3"
PREVIOUS_TAG = "v1.2.2"
OLDER_TAG = "v1.2.0"
ALL_TAGS = [OLDER_TAG, PREVIOUS_TAG, CURRENT_TAG]


def portfile(sha512: str) -> str:
    return (
        "# SHA512 here after the tag exists (docs/RELEASE.md).\n"
        "vcpkg_from_github(\n"
        "    REPO owner/repo\n"
        f"    SHA512 {sha512}\n"
        "    HEAD_REF main\n"
        ")\n")


# spec: SWR-ADOPT-0009
class Parse(unittest.TestCase):
    def test_the_version_is_read_from_the_manifest(self) -> None:
        self.assertEqual(guard.manifest_version(json.dumps({"version": VERSION})), VERSION)

    def test_a_manifest_without_a_release_version_is_undecided(self) -> None:
        for text in ("{}", json.dumps({"version": "1.2"}), json.dumps({"version": 3}), "not json"):
            with self.subTest(text=text), self.assertRaises(guard.Undecided):
                guard.manifest_version(text)

    def test_the_hash_is_the_argument_not_the_comment(self) -> None:
        self.assertEqual(guard.portfile_sha512(portfile(NEW_HASH)), NEW_HASH)

    def test_a_portfile_without_a_hash_names_none(self) -> None:
        self.assertIsNone(guard.portfile_sha512("# SHA512 here after the tag exists\n"))
        self.assertIsNone(guard.portfile_sha512("SHA512 abc\n"))

    def test_the_previous_tag_is_the_highest_below_the_version(self) -> None:
        self.assertEqual(guard.previous_tag(ALL_TAGS + ["v1.2.10", "v0.9.9", "nightly"], VERSION), PREVIOUS_TAG)

    def test_versions_compare_as_numbers_not_text(self) -> None:
        self.assertEqual(guard.previous_tag(["v1.9.0", "v1.10.0"], "1.11.0"), "v1.10.0")

    def test_the_first_release_has_no_previous_tag(self) -> None:
        self.assertIsNone(guard.previous_tag([CURRENT_TAG, "v1.3.0"], VERSION))

    def test_ls_remote_lists_each_tag_once(self) -> None:
        output = ("1111\trefs/tags/v1.2.2\n2222\trefs/tags/v1.2.3\n3333\trefs/tags/v1.2.3^{}\n"
                  "4444\trefs/heads/main\n")
        self.assertEqual(guard.parse_ls_remote(output), [PREVIOUS_TAG, CURRENT_TAG])


# spec: SWR-ADOPT-0009
class Decide(unittest.TestCase):
    def verdict(self, committed: str | None, tags: list[str], held: dict[str, str | None]) -> guard.Verdict:
        return guard.decide(VERSION, committed, tags, held)

    def test_a_missing_tag_skips(self) -> None:
        verdict = self.verdict(NEW_HASH, [OLDER_TAG, PREVIOUS_TAG], {})
        self.assertFalse(verdict.run)
        self.assertIn(CURRENT_TAG, verdict.reason)
        self.assertIn("not tagged", verdict.reason)

    def test_a_missing_tag_skips_even_with_a_new_hash(self) -> None:
        self.assertFalse(self.verdict(NEW_HASH, [PREVIOUS_TAG], {PREVIOUS_TAG: OLD_HASH}).run)

    def test_a_hash_still_held_at_the_tag_skips(self) -> None:
        verdict = self.verdict(OLD_HASH, ALL_TAGS, {CURRENT_TAG: OLD_HASH, PREVIOUS_TAG: OLDER_HASH})
        self.assertFalse(verdict.run)
        self.assertIn(CURRENT_TAG, verdict.reason)

    def test_a_hash_still_held_at_the_previous_tag_skips(self) -> None:
        verdict = self.verdict(OLD_HASH, ALL_TAGS, {CURRENT_TAG: NEW_HASH, PREVIOUS_TAG: OLD_HASH})
        self.assertFalse(verdict.run)
        self.assertIn(PREVIOUS_TAG, verdict.reason)

    def test_a_new_hash_with_the_tag_present_runs(self) -> None:
        verdict = self.verdict(NEW_HASH, ALL_TAGS, {CURRENT_TAG: OLD_HASH, PREVIOUS_TAG: OLDER_HASH})
        self.assertTrue(verdict.run)

    def test_a_tag_whose_port_does_not_exist_holds_no_hash_to_match(self) -> None:
        self.assertTrue(self.verdict(NEW_HASH, ALL_TAGS, {CURRENT_TAG: None, PREVIOUS_TAG: None}).run)

    def test_the_first_release_runs_against_its_own_tag_only(self) -> None:
        self.assertTrue(self.verdict(NEW_HASH, [CURRENT_TAG], {CURRENT_TAG: OLD_HASH}).run)
        self.assertFalse(self.verdict(OLD_HASH, [CURRENT_TAG], {CURRENT_TAG: OLD_HASH}).run)

    def test_a_portfile_without_a_hash_skips(self) -> None:
        self.assertFalse(self.verdict(None, ALL_TAGS, {}).run)


def run_git(root: pathlib.Path, *arguments: str) -> None:
    subprocess.run(
        ["git", "-C", str(root), "-c", "user.name=test", "-c", "user.email=test@example.invalid",
         "-c", "commit.gpgSign=false", "-c", "tag.gpgSign=false", *arguments],
        check=True, capture_output=True)


# spec: SWR-ADOPT-0009
class GitRepository(unittest.TestCase):
    """The verdict read from a repository whose tags are releases."""

    def setUp(self) -> None:
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        self.root = pathlib.Path(holder.name) / "repository"
        self.root.mkdir()
        run_git(self.root, "init", "--quiet")

    def commit(self, version: str, sha512: str | None, tag: str | None = None) -> None:
        manifest = self.root / guard.VCPKG_MANIFEST
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"version": version}), encoding="utf-8")
        (self.root / guard.PORTFILE).write_text(
            portfile(sha512) if sha512 else "# no hash yet\n", encoding="utf-8")
        run_git(self.root, "add", "--all")
        run_git(self.root, "commit", "--quiet", "--allow-empty", "-m", f"state {version}")
        if tag:
            run_git(self.root, "tag", tag)

    def verdict(self) -> guard.Verdict:
        return guard.read_verdict(self.root, str(self.root))

    def test_a_release_pull_request_skips_because_the_tag_is_missing(self) -> None:
        self.commit("1.2.2", OLDER_HASH, PREVIOUS_TAG)
        self.commit(VERSION, OLD_HASH)
        self.assertFalse(self.verdict().run)

    def test_a_tagged_release_with_the_old_hash_skips(self) -> None:
        self.commit("1.2.2", OLDER_HASH, PREVIOUS_TAG)
        self.commit(VERSION, OLD_HASH, CURRENT_TAG)
        verdict = self.verdict()
        self.assertFalse(verdict.run)
        self.assertIn("has not moved it", verdict.reason)

    def test_the_post_tag_commit_that_moves_the_hash_runs(self) -> None:
        self.commit("1.2.2", OLDER_HASH, PREVIOUS_TAG)
        self.commit(VERSION, OLD_HASH, CURRENT_TAG)
        self.commit(VERSION, NEW_HASH)
        self.assertTrue(self.verdict().run)

    def test_a_previous_tag_that_predates_the_port_holds_no_hash(self) -> None:
        self.commit("1.0.0", None, OLDER_TAG)
        self.commit(VERSION, OLD_HASH, CURRENT_TAG)
        self.commit(VERSION, NEW_HASH)
        self.assertTrue(self.verdict().run)

    def test_a_remote_tag_missing_from_the_clone_is_undecided(self) -> None:
        self.commit(VERSION, NEW_HASH, CURRENT_TAG)
        clone = self.root.parent / "clone"
        subprocess.run(["git", "clone", "--quiet", "--no-tags", str(self.root), str(clone)],
                       check=True, capture_output=True)
        with self.assertRaises(guard.Undecided):
            guard.read_verdict(clone, str(self.root))

    def test_an_unreachable_remote_is_undecided(self) -> None:
        self.commit(VERSION, NEW_HASH)
        with self.assertRaises(guard.Undecided):
            guard.read_verdict(self.root, str(self.root.parent / "nowhere"))

    def test_main_writes_the_outputs_for_the_workflow(self) -> None:
        self.commit(VERSION, OLD_HASH, CURRENT_TAG)
        self.commit(VERSION, NEW_HASH)
        sink = self.root.parent / "output"
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            status = guard.main(["--root", str(self.root), "--remote", str(self.root),
                                 "--github-output", str(sink)])
        self.assertEqual(status, guard.EXIT_DECIDED)
        self.assertIn("run=true", sink.read_text(encoding="utf-8").splitlines())
        self.assertNotIn("::notice", stdout.getvalue())

    def test_main_names_a_skip_in_a_notice(self) -> None:
        self.commit(VERSION, OLD_HASH)
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            status = guard.main(["--root", str(self.root), "--remote", str(self.root)])
        self.assertEqual(status, guard.EXIT_DECIDED)
        self.assertIn("run=false", stdout.getvalue())
        self.assertIn("::notice", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
