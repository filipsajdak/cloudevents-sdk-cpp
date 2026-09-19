"""Shared loading of requirement markdown files (YAML frontmatter + body).

Adapted from the reqmd-poc proof of concept; generic to any product.
"""

from __future__ import annotations

import dataclasses
import os
import pathlib
import re

import yaml

# The spec tree is nested under spec/ (the isildur layout), so the spec root and
# the repository root are different directories. Requirement, schema and profile
# files are addressed from SPEC_ROOT; code:/doc:/test: references in requirement
# frontmatter are repository paths and resolve from REPO_ROOT.
SPEC_ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO_ROOT = SPEC_ROOT.parent
REQ_DIR = SPEC_ROOT / "requirements"
SCHEMA_PATH = SPEC_ROOT / "schema" / "requirement.schema.json"
PROFILES_PATH = SPEC_ROOT / "schema" / "profiles.yml"

LEVEL_DIRS = {"stakeholder": "STK", "system": "SYS", "software": "SWR"}
PARENT_LEVEL = {"software": "system", "system": "stakeholder", "stakeholder": None}


@dataclasses.dataclass
class Requirement:
    path: pathlib.Path
    meta: dict
    body: str

    @property
    def uid(self) -> str:
        return self.meta.get("uid", "<missing-uid>")


class FrontmatterError(Exception):
    pass


def parse_requirement_file(path: pathlib.Path) -> Requirement:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        raise FrontmatterError(f"{path}: file does not start with '---' frontmatter")
    try:
        _, fm, body = text.split("---\n", 2)
    except ValueError as exc:
        raise FrontmatterError(f"{path}: unterminated frontmatter block") from exc
    try:
        meta = yaml.safe_load(fm)
    except yaml.YAMLError as exc:
        raise FrontmatterError(f"{path}: invalid YAML frontmatter: {exc}") from exc
    if not isinstance(meta, dict):
        raise FrontmatterError(f"{path}: frontmatter is not a mapping")
    return Requirement(path=path, meta=meta, body=body.strip())


def load_requirements() -> list[Requirement]:
    # Only the three 29148 levels carry the requirement schema; other
    # folders under requirements/ (e.g. change-request/) are not graded as
    # requirements and are rendered separately.
    reqs = []
    for level in LEVEL_DIRS:
        level_dir = REQ_DIR / level
        if not level_dir.is_dir():
            continue
        for path in sorted(level_dir.glob("*.md")):
            reqs.append(parse_requirement_file(path))
    return reqs


CR_DIR = REQ_DIR / "change-request"


def load_change_requests() -> list[Requirement]:
    # Change requests are frontmatter records (uid, title, status, impacts,
    # ...) with a markdown analysis body. They are not graded as requirements;
    # md_to_needs renders them into a {cr} need per file.
    crs = []
    if CR_DIR.is_dir():
        for path in sorted(CR_DIR.glob("*.md")):
            crs.append(parse_requirement_file(path))
    return crs


def load_active_profile() -> dict:
    data = yaml.safe_load(PROFILES_PATH.read_text(encoding="utf-8"))
    name = data["active_profile"]
    profile = data["profiles"][name]
    profile["name"] = name
    return profile


ACCEPTANCE_DIR = REPO_ROOT / "docs" / "acceptance"


def load_acceptance_scenarios() -> dict:
    # The acceptance-scenario registry: one markdown file per scenario under
    # docs/acceptance/, mirroring the requirement model (frontmatter + body).
    # Returns id -> {kind, title, description, verifies}; the id is the filename
    # stem (authoritative). Resolves acc:/val: verification references
    # (verified_by) to real, linkable pages. Returns {} when the directory is
    # absent so the tooling degrades gracefully in a partially-scaffolded repo.
    if not ACCEPTANCE_DIR.is_dir():
        return {}
    scenarios: dict = {}
    for path in sorted(ACCEPTANCE_DIR.glob("*.md")):
        scenario = parse_requirement_file(path)
        scenarios[path.stem] = {
            "kind": scenario.meta.get("kind", "acc"),
            "title": scenario.meta.get("title", ""),
            "description": scenario.body,
            "verifies": scenario.meta.get("verifies") or [],
        }
    return scenarios


def repo_blob_base() -> str:
    # Base URL for linking code:/doc:/test: file references to the source
    # browser, e.g. https://gitlab.com/<repo>/-/blob/main. Prefer the CI-provided
    # project URL + ref; fall back to the GitLab backend declared in
    # admin/config.yml so local spec builds link too. Returns "" if unknown (the
    # linkifier then degrades to a relative href).
    project = os.environ.get("CI_PROJECT_URL")
    if project:
        ref = os.environ.get("CI_COMMIT_REF_NAME") or "main"
        return f"{project.rstrip('/')}/-/blob/{ref}"
    cfg = REPO_ROOT / "admin" / "config.yml"
    if cfg.is_file():
        base, repo, branch = "https://gitlab.com", None, "main"
        for line in cfg.read_text(encoding="utf-8").splitlines():
            for key, target in (("base_url", "base"), ("repo", "repo"), ("branch", "branch")):
                m = re.match(rf"\s*{key}:\s*(.+?)\s*$", line)
                if m:
                    val = m.group(1).strip().strip('"').strip("'")
                    if target == "base":
                        base = val.rstrip("/")
                    elif target == "repo":
                        repo = val
                    else:
                        branch = val
        if repo:
            return f"{base}/{repo}/-/blob/{branch}"
    return ""
