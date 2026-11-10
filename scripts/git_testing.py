"""Keeps a test that builds a throwaway git repository out of the real one.

Git exports GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE and the like into the
environment of a `git rebase --exec` command and of every hook. A test run from
there that calls `git -C <tmp> init` then commits and tags in the repository the
variables name, not in <tmp>. `isolate` removes those variables, and the
configuration that could change a commit, from the process environment for the
length of one test case.

Standard-library Python; the suites import it as `git_testing`.
"""

from __future__ import annotations

import os
import unittest
from unittest import mock

# What `git rev-parse --local-env-vars` lists (scripts/test_git_testing.py checks
# that this stays a superset of it), plus the two that also steer where git looks.
REPOSITORY_SELECTORS = (
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_CEILING_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_CONFIG",
    "GIT_CONFIG_COUNT",
    "GIT_CONFIG_PARAMETERS",
    "GIT_DIR",
    "GIT_GRAFT_FILE",
    "GIT_IMPLICIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_NAMESPACE",
    "GIT_NO_REPLACE_OBJECTS",
    "GIT_OBJECT_DIRECTORY",
    "GIT_PREFIX",
    "GIT_REPLACE_REF_BASE",
    "GIT_SHALLOW_FILE",
    "GIT_WORK_TREE",
)

# The user's and the machine's configuration (signing, hooks, templates) must not
# decide what a throwaway repository does.
CONFIG_ISOLATION = {"GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull}


def isolate(case: unittest.TestCase) -> None:
    """Scrub the git environment of this process until `case` finishes.

    The environment is the process's, not a copy handed to one subprocess, so the
    tools a test calls in-process (which run git themselves) are covered too.
    """
    patch = mock.patch.dict(os.environ, CONFIG_ISOLATION)
    patch.start()
    case.addCleanup(patch.stop)
    for name in REPOSITORY_SELECTORS:
        os.environ.pop(name, None)
