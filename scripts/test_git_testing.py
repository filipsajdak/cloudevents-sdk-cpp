"""Suites for scripts/git_testing.py: the suites that build git repositories stay in them.

Registered with CTest as `git_testing_tools`; standard-library Python only
(git is needed, and the delivered-in suites also pyyaml, which they skip without):

    python3 -m unittest discover -s scripts -p 'test_git_testing*.py'

Test infrastructure, so there is no requirement to cite.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import git_testing  # noqa: E402

SCRIPTS = pathlib.Path(__file__).resolve().parent
# The suites that run git against a repository they build, one discover pattern each.
# The mutation script's suites are narrowed to one class: the sentinel check needs
# one case that commits, not the whole file.
BUILDERS = (
    ("test_vcpkg_release_guard.py", None),
    ("test_derive_delivered_in.py", None),
    ("test_mutation_summary.py", "BaseRefHandling"),
)
IDENTITY = ["-c", "user.name=s", "-c", "user.email=s@example.invalid", "-c", "commit.gpgSign=false"]


def run_git(root: pathlib.Path, *arguments: str) -> str:
    return subprocess.run(["git", *IDENTITY, "-C", str(root), *arguments],
                          capture_output=True, text=True, check=True).stdout


@unittest.skipUnless(shutil.which("git"), "needs git")
class SelectorList(unittest.TestCase):
    def test_the_list_covers_every_variable_git_says_selects_a_repository(self) -> None:
        git_testing.isolate(self)
        listed = set(run_git(pathlib.Path.cwd(), "rev-parse", "--local-env-vars").split())
        self.assertLessEqual(listed, set(git_testing.REPOSITORY_SELECTORS))


@unittest.skipUnless(shutil.which("git"), "needs git")
class HijackedEnvironment(unittest.TestCase):
    """What a git rebase --exec command or a hook environment does to a suite."""

    def setUp(self) -> None:
        git_testing.isolate(self)
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        self.sentinel = pathlib.Path(holder.name) / "sentinel"
        self.sentinel.mkdir()
        run_git(self.sentinel, "init", "--quiet")
        (self.sentinel / "kept.txt").write_text("kept\n", encoding="utf-8")
        run_git(self.sentinel, "add", "--all")
        run_git(self.sentinel, "commit", "--quiet", "-m", "sentinel")
        run_git(self.sentinel, "tag", "sentinel-tag")

    def snapshot(self) -> dict[str, object]:
        git_dir = self.sentinel / ".git"
        return {
            "refs": run_git(self.sentinel, "for-each-ref"),
            "head": (git_dir / "HEAD").read_bytes(),
            "index": (git_dir / "index").read_bytes(),
            "status": run_git(self.sentinel, "status", "--porcelain", "--untracked-files=all"),
        }

    def hijacked(self) -> dict[str, str]:
        return {**os.environ, "GIT_DIR": str(self.sentinel / ".git"), "GIT_WORK_TREE": str(self.sentinel),
                "GIT_INDEX_FILE": str(self.sentinel / ".git" / "index")}

    def test_the_suites_that_build_repositories_leave_the_repository_the_environment_names_untouched(self) -> None:
        before = self.snapshot()
        for pattern, only in BUILDERS:
            with self.subTest(suite=pattern):
                command = [sys.executable, "-I", "-m", "unittest", "discover", "-s", str(SCRIPTS), "-p", pattern]
                if only:
                    command += ["-k", only]
                done = subprocess.run(command, capture_output=True, text=True, env=self.hijacked(), check=False)
                self.assertEqual(done.returncode, 0, done.stderr)
                self.assertEqual(self.snapshot(), before)


if __name__ == "__main__":
    unittest.main()
