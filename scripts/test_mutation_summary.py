"""Suites for scripts/mutation_summary.py and the behaviour of mutation.sh.

Registered with CTest as `mutation_tools`; standard-library Python only:

    python3 -m unittest discover -s scripts -p 'test_*.py'
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
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



# Exit codes of mutation.sh beyond usage (2): see the header of the script.
EXIT_NO_MUTANTS = 3
EXIT_NO_CONFIG = 4
STUB_SUITE = "stub_test"
NEW_HEADER = "include/cloudevents/detail/added.hpp"
OLD_HEADER = "include/cloudevents/detail/kept.hpp"
# How long the stub awk lingers after it has printed, so its exit lands while the
# script is blocked writing, and how long the reader waits before it drains the pipe.
LINGER_SECONDS = 2
DRAIN_AFTER_SECONDS = 2 * LINGER_SECONDS
FILL_CHUNK = b"x" * 4096
STUB_ENV_KEYS = ("CE_MULL_LLVM", "CE_MULL_DIR", "CE_MULL_VERSION", "CE_MULL_BUILD")


def git(repo: Path, *args: str) -> str:
    env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True, env=env).stdout.strip()


def executable(path: Path, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body)
    path.chmod(0o755)


@unittest.skipUnless(os.name == "posix" and shutil.which("git"), "mutation.sh needs a POSIX shell and git")
class Scenario(unittest.TestCase):
    """mutation.sh in a throwaway repository, with a stub in place of mull-runner."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        tmp = Path(self._tmp.name).resolve()
        self.repo = tmp / "repo"
        (self.repo / "scripts").mkdir(parents=True)
        for name in ("mutation.sh", "mutation_summary.py"):
            shutil.copy(SCRIPTS / name, self.repo / "scripts" / name)
        shutil.copy(SCRIPTS.parent / "mull.yml", self.repo / "mull.yml")
        git(self.repo, "init", "-q")
        (self.repo / OLD_HEADER).parent.mkdir(parents=True)
        (self.repo / OLD_HEADER).write_text("int kept(int a) { return a + 1; }\n")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "base")
        self.base = git(self.repo, "rev-parse", "HEAD")
        self.build = tmp / "build"
        self.mull = tmp / "mull"
        self.llvm = tmp / "llvm"
        executable(self.llvm / "bin" / "clang++", "#!/bin/sh\n")
        (self.mull / "lib").mkdir(parents=True)
        (self.mull / "lib" / "mull-ir-frontend-21").write_text("")
        executable(self.build / STUB_SUITE, "#!/bin/sh\n")
        self.other_cwd = tmp / "elsewhere"
        self.other_cwd.mkdir()

    def stub_runner(self, body: str) -> None:
        executable(self.mull / "bin" / "mull-runner-21", "#!/bin/sh\n" + body)

    def run_script(self, *args: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
        env = {k: v for k, v in os.environ.items() if k not in STUB_ENV_KEYS}
        env.update(CE_MULL_LLVM=str(self.llvm), CE_MULL_DIR=str(self.mull), CE_MULL_BUILD=str(self.build))
        return subprocess.run(
            ["bash", str(self.repo / "scripts" / "mutation.sh"), *args],
            capture_output=True, text=True, cwd=cwd or self.other_cwd, env=env,
        )

    def commit_change(self, path: str, text: str) -> None:
        (self.repo / path).write_text(text)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "change")

    def killed_line(self) -> str:
        return f"{self.repo}/{NEW_HEADER}:1:20: warning: Killed: Replaced + with - [cxx_add_to_sub]"


class ConfigAndWorkingDirectory(Scenario):
    def setUp(self) -> None:
        super().setUp()
        self.commit_change(OLD_HEADER, "int kept(int a) { return a - 1; }\n")

    def plan(self, cwd: Path) -> dict[str, str]:
        run = self.run_script(self.base, "--dry-run", "--suites", STUB_SUITE, cwd=cwd)
        self.assertEqual(run.returncode, 0, run.stderr)
        return {line.split(":", 1)[0]: line.split(":", 1)[1].strip() for line in run.stdout.splitlines() if line.startswith(("cwd:", "command:"))}

    def test_the_dry_run_prints_the_filter_and_the_suite(self) -> None:
        run = self.run_script(self.base, "--dry-run", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("gitDiffRef:", run.stdout)
        self.assertIn(f"gitProjectRoot: {self.repo}", run.stdout)
        self.assertIn(STUB_SUITE, run.stdout)

    def test_the_runner_runs_in_the_build_dir_with_the_config_named(self) -> None:
        plan = self.plan(self.other_cwd)
        self.assertEqual(plan["cwd"], str(self.build))
        self.assertIn(f"MULL_CONFIG={self.build}/mull.yml", plan["command"])
        self.assertIn(f"{self.mull}/bin/mull-runner-21", plan["command"])

    def test_the_plan_does_not_depend_on_where_the_script_is_launched_from(self) -> None:
        here = self.plan(self.other_cwd)
        self.assertEqual(here, self.plan(self.repo))
        self.assertEqual(here, self.plan(self.repo / "include"))

    def test_the_runner_gets_the_config_through_the_environment(self) -> None:
        self.stub_runner('echo "config=$MULL_CONFIG cwd=$(pwd -P)"\n')
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        log = (self.build / f"{STUB_SUITE}.mull.log").read_text()
        self.assertIn(f"config={self.build}/mull.yml", log)
        self.assertIn(f"cwd={self.build.resolve()}", log)


class InterruptedWrites(Scenario):
    """A signal that arrives while the script is blocked writing must not fail it.

    A child the script started and did not wait for (a process substitution) exits
    later and raises SIGCHLD; bash does not restart a write it interrupts, and its
    echo then reports "write error: Interrupted system call" and exits 1. The stub
    awk makes that child outlive the script's first writes, and the reader keeps
    stdout full so the script is blocked in a write when it exits.
    """

    def setUp(self) -> None:
        super().setUp()
        self.commit_change(OLD_HEADER, "int kept(int a) { return a - 1; }\n")
        real_awk = shutil.which("awk")
        assert real_awk
        executable(self.repo.parent / "bin" / "awk", f'#!/bin/sh\n{real_awk} "$@"\nsleep {LINGER_SECONDS}\n')

    def full_pipe(self) -> tuple[int, int]:
        reader, writer = os.pipe()
        os.set_blocking(writer, False)
        try:
            while True:
                os.write(writer, FILL_CHUNK)
        except BlockingIOError:
            pass
        os.set_blocking(writer, True)
        return reader, writer

    def test_a_child_exiting_while_stdout_is_full_does_not_fail_the_script(self) -> None:
        # bash 3.2, the macOS /bin/bash, is the one that reports the interrupted write.
        for shell in dict.fromkeys(["bash", *filter(os.path.exists, ["/bin/bash"])]):
            with self.subTest(shell=shell):
                self.run_blocked_on_stdout(shell)

    def run_blocked_on_stdout(self, shell: str) -> None:
        reader, writer = self.full_pipe()
        env = {k: v for k, v in os.environ.items() if k not in STUB_ENV_KEYS}
        env.update(CE_MULL_LLVM=str(self.llvm), CE_MULL_DIR=str(self.mull), CE_MULL_BUILD=str(self.build))
        env["PATH"] = f"{self.repo.parent / 'bin'}{os.pathsep}{env['PATH']}"
        script = self.repo / "scripts" / "mutation.sh"
        with subprocess.Popen(
            [shell, str(script), self.base, "--dry-run", "--suites", STUB_SUITE],
            stdout=writer, stderr=subprocess.PIPE, text=True, cwd=self.other_cwd, env=env,
        ) as proc:
            os.close(writer)
            time.sleep(DRAIN_AFTER_SECONDS)
            while os.read(reader, len(FILL_CHUNK)):
                pass
            stderr = proc.stderr.read()
            self.assertEqual(proc.wait(), 0, stderr)
        os.close(reader)


class BaseRefHandling(Scenario):
    def test_a_header_the_base_lacks_counts_as_changed(self) -> None:
        self.commit_change(NEW_HEADER, "int added(int a) {\n  return a + 1;\n}\n")
        run = self.run_script(self.base, "--dry-run", "--suites", STUB_SUITE)
        self.assertIn("changed:    3 lines in 1 files", run.stdout)

    def test_the_diff_reference_is_a_commit_holding_an_empty_copy_of_the_new_header(self) -> None:
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        run = self.run_script(self.base, "--dry-run", "--suites", STUB_SUITE)
        tree = next(line.split()[-1] for line in run.stdout.splitlines() if line.startswith("diff tree:"))
        self.assertEqual(git(self.repo, "cat-file", "-t", tree), "commit")
        self.assertEqual(git(self.repo, "cat-file", "-s", f"{tree}:{NEW_HEADER}"), "0")

    def test_the_run_leaves_nothing_untracked_and_no_ref(self) -> None:
        self.stub_runner(f'echo "{self.killed_line()}"\n')
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        refs = git(self.repo, "for-each-ref")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(git(self.repo, "status", "--porcelain", "--untracked-files=all"), "")
        self.assertEqual(git(self.repo, "for-each-ref"), refs)


class ZeroMutantsGuard(Scenario):
    def test_changed_lines_and_no_mutant_fail_loudly(self) -> None:
        self.stub_runner("exit 0\n")
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, EXIT_NO_MUTANTS)
        self.assertIn("1 lines changed under include/ but Mull ran no mutant", run.stderr)

    def test_a_mutant_run_prints_the_line_and_exits_zero(self) -> None:
        self.stub_runner(f'echo "{self.killed_line()}"\n')
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("Mutation: Mull, changed lines: 1 killed, 0 survived", run.stdout)

    def test_no_changed_cpp_lines_is_nothing_to_mutate_and_exit_zero(self) -> None:
        self.stub_runner("exit 0\n")
        self.commit_change("README.md", "docs only\n")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("nothing to mutate", run.stdout)
        self.assertIn("Mutation: none, because", run.stdout)

    def test_a_config_mull_could_not_read_fails_loudly(self) -> None:
        self.stub_runner('echo "[warning] Mull cannot find config (mull.yml). Using some defaults."\n')
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, EXIT_NO_CONFIG)

    def test_a_diff_reference_mull_could_not_resolve_fails_loudly(self) -> None:
        self.stub_runner('echo "[warning] GitDiffFilter: cannot resolve \'abc\' to a commit"\n')
        self.commit_change(NEW_HEADER, "int added(int a) { return a + 1; }\n")
        run = self.run_script(self.base, "--no-build", "--suites", STUB_SUITE)
        self.assertEqual(run.returncode, EXIT_NO_CONFIG)


if __name__ == "__main__":
    unittest.main()
