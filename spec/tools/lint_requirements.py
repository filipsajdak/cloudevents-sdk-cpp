#!/usr/bin/env python3
"""Quality gate for requirement files.

Checks, per requirement file:
- frontmatter validates against schema/requirement.schema.json
- uid matches the filename and the level directory it lives in
- uid is unique across the whole repository
- security_classification is an allowed value of the active sector profile
- the statement body is non-empty, contains exactly one 'shall', and its
  opening matches an EARS pattern (The/When/While/If/Where ... shall ...)
- the statement avoids ambiguous terms (INCOSE Guide to Writing Requirements)

Exit code 0 if clean, 1 with a finding list otherwise.

Adapted from the reqmd-poc proof of concept.
"""

from __future__ import annotations

import json
import re
import sys

import jsonschema

import reqlib

EARS_RE = re.compile(r"^(The|When|While|If|Where)\b.*\bshall\b", re.IGNORECASE | re.DOTALL)

AMBIGUOUS_TERMS = [
    "user-friendly", "user friendly", "appropriate", "adequate", "as required",
    "if possible", "as applicable", "etc", "and/or", "easy to", "quickly",
    "approximately", "sufficient", "robust", "seamless", "best effort",
    "minimal", "maximize", "minimize", "optimal", "state of the art",
]


def main() -> int:
    schema = json.loads(reqlib.SCHEMA_PATH.read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema)
    profile = reqlib.load_active_profile()

    findings: list[str] = []
    seen_uids: dict[str, str] = {}

    try:
        reqs = reqlib.load_requirements()
    except reqlib.FrontmatterError as exc:
        print(f"FAIL: {exc}")
        return 1

    for req in reqs:
        rel = req.path.relative_to(reqlib.REPO_ROOT)

        for error in sorted(validator.iter_errors(req.meta), key=str):
            loc = "/".join(str(p) for p in error.absolute_path) or "<root>"
            findings.append(f"{rel}: schema: {loc}: {error.message}")

        uid = req.meta.get("uid")
        if isinstance(uid, str):
            if req.path.stem != uid:
                findings.append(f"{rel}: filename must equal uid ('{uid}.md')")
            if uid in seen_uids:
                findings.append(f"{rel}: duplicate uid '{uid}' (also in {seen_uids[uid]})")
            else:
                seen_uids[uid] = str(rel)
            level = req.meta.get("type")
            expected_prefix = reqlib.LEVEL_DIRS.get(level)
            if expected_prefix and not uid.startswith(expected_prefix + "-"):
                findings.append(f"{rel}: uid prefix does not match type '{level}' (expected {expected_prefix}-)")
            if level and req.path.parent.name != level:
                findings.append(f"{rel}: file is in '{req.path.parent.name}/' but type is '{level}'")

        sc = req.meta.get("security_classification")
        if sc is not None and sc not in profile["security_classification"]:
            findings.append(
                f"{rel}: security_classification '{sc}' not allowed by active profile "
                f"'{profile['name']}' ({profile['standard']}); allowed: {profile['security_classification']}"
            )

        body = req.body
        if not body:
            findings.append(f"{rel}: requirement statement body is empty")
            continue
        shall_count = len(re.findall(r"\bshall\b", body, re.IGNORECASE))
        if shall_count == 0:
            findings.append(f"{rel}: statement contains no 'shall'")
        if not EARS_RE.match(body):
            findings.append(
                f"{rel}: statement does not match an EARS pattern "
                "(must start with The/When/While/If/Where and contain 'shall')"
            )
        lowered = body.lower()
        for term in AMBIGUOUS_TERMS:
            if re.search(rf"\b{re.escape(term)}\b", lowered):
                findings.append(f"{rel}: ambiguous term '{term}' in statement")

    if findings:
        print(f"FAIL: {len(findings)} finding(s) in {len(reqs)} requirement(s):")
        for f in findings:
            print(f"  - {f}")
        return 1

    print(f"OK: {len(reqs)} requirement(s) passed lint (profile: {profile['name']}/{profile['standard']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
