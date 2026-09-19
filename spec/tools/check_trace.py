#!/usr/bin/env python3
"""Bidirectional traceability gate.

Rules:
- every non-stakeholder requirement has at least one derived_from parent
- every derived_from link resolves to an existing requirement exactly one
  level up (software -> system, system -> stakeholder)
- a requirement with status approved/implemented/verified has a
  verification_method and at least one verified_by reference
- an approved (or later) stakeholder/system requirement has at least one
  child requirement (no unrefined approved requirements)

Optionally writes the full traceability matrix as CSV (--matrix-out).
Exit code 0 if clean, 1 with a finding list otherwise.

Adapted from the reqmd-poc proof of concept.
"""

from __future__ import annotations

import argparse
import collections
import csv
import re
import sys

import reqlib

NEEDS_VERIFICATION = {"approved", "implemented", "verified"}
DONE_STATUSES = {"implemented", "verified"}


def _submodule_prefixes() -> tuple[str, ...]:
    # Paths under a git submodule are not present (or authoritative) in this
    # repo -- reviewed in their own repo -- so we link to them but never
    # existence-check them here. Auto-discovered from .gitmodules so this stays
    # generic across projects.
    gm = reqlib.REPO_ROOT / ".gitmodules"
    prefixes = []
    if gm.is_file():
        for line in gm.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*path\s*=\s*(.+?)\s*$", line)
            if m:
                prefixes.append(m.group(1).rstrip("/") + "/")
    return tuple(prefixes)


SUBMODULE_PREFIXES = _submodule_prefixes()


def _under_submodule(path: str) -> bool:
    return any(path.startswith(p) for p in SUBMODULE_PREFIXES)


def resolve_reference(ref: str, acc_ids: set[str]) -> tuple[str, str] | None:
    """Resolve a satisfied_by/verified_by reference to its target.

    Returns None when the reference resolves (or is un-checkable, e.g. lives in
    a submodule). Otherwise returns (reason, kind):
    - kind "dangling": the target does not exist (missing repo file, a
      path-qualified test whose file is absent, or an unknown acc:/val: id).
      Severity is tiered by the referencing requirement's status.
    - kind "migration": the reference is well-formed but not yet in the target
      shape (a bare, un-path-qualified test: ref). Always a non-fatal warning --
      the test may well exist; it just is not linkable until it names its file.

    Prefixes mirror schema/requirement.schema.json: code:/doc: -> repo path,
    test: -> path-qualified test file (test:<path>::<name>), acc:/val: -> an id
    defined in the acceptance-scenario registry.
    """
    prefix, _, rest = ref.partition(":")
    if prefix in ("code", "doc"):
        if _under_submodule(rest):
            return None
        return None if (reqlib.REPO_ROOT / rest).exists() else (f"'{ref}' path does not exist", "dangling")
    if prefix == "test":
        if "::" not in rest:
            return (f"'{ref}' is not path-qualified (expected test:<path>::<name>)", "migration")
        path = rest.split("::", 1)[0]
        if _under_submodule(path):
            return None
        return None if (reqlib.REPO_ROOT / path).exists() else (f"'{ref}' path does not exist", "dangling")
    if prefix in ("acc", "val"):
        if rest in acc_ids:
            return None
        return (f"'{ref}' has no docs/acceptance/{rest}.md entry", "dangling")
    # Unknown prefix: shape is validated by lint_requirements against the schema.
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix-out", help="write traceability matrix CSV to this path")
    args = parser.parse_args()

    try:
        reqs = reqlib.load_requirements()
    except reqlib.FrontmatterError as exc:
        print(f"FAIL: {exc}")
        return 1

    by_uid = {r.uid: r for r in reqs}
    children = collections.defaultdict(list)
    acc_ids = set(reqlib.load_acceptance_scenarios().keys())
    findings: list[str] = []
    # Reference-resolution issues are tiered by status: a dangling reference on a
    # delivered (implemented|verified) requirement is a hard finding; on a
    # draft/approved one it is a non-fatal warning (the backlog is cleaned as
    # requirements mature). obsolete requirements are skipped entirely.
    warnings: list[str] = []

    for req in reqs:
        for parent_uid in req.meta.get("derived_from") or []:
            children[parent_uid].append(req.uid)

    for req in reqs:
        rel = req.path.relative_to(reqlib.REPO_ROOT)
        level = req.meta.get("type")
        status = req.meta.get("status")
        parents = req.meta.get("derived_from") or []
        parent_level = reqlib.PARENT_LEVEL.get(level)

        if parent_level and not parents:
            findings.append(f"{rel}: {level} requirement has no derived_from parent")

        for parent_uid in parents:
            parent = by_uid.get(parent_uid)
            if parent is None:
                findings.append(f"{rel}: derived_from '{parent_uid}' does not exist")
            elif parent.meta.get("type") != parent_level:
                findings.append(
                    f"{rel}: derived_from '{parent_uid}' is a {parent.meta.get('type')} "
                    f"requirement; a {level} requirement must trace to {parent_level}"
                )

        if status in NEEDS_VERIFICATION:
            if not req.meta.get("verification_method"):
                findings.append(f"{rel}: status '{status}' requires a verification_method")
            if not (req.meta.get("verified_by") or []):
                findings.append(f"{rel}: status '{status}' requires at least one verified_by")
            if level != "software" and not children.get(req.uid):
                findings.append(
                    f"{rel}: {level} requirement with status '{status}' has no child requirements"
                )

        # Reference resolution (satisfied_by / verified_by): every identifier
        # must resolve to a real target -- a repo file, a path-qualified test, or
        # an acceptance-scenario registry id. Tiered by status.
        if status != "obsolete":
            hard = status in DONE_STATUSES
            for field in ("satisfied_by", "verified_by"):
                for ref in req.meta.get(field) or []:
                    resolved = resolve_reference(str(ref), acc_ids)
                    if not resolved:
                        continue
                    reason, kind = resolved
                    # A genuinely dangling target is a hard finding on a
                    # delivered requirement; a bare (un-path-qualified) test ref
                    # is always just a migration warning.
                    sink = findings if (kind == "dangling" and hard) else warnings
                    sink.append(f"{rel}: {field} {reason}")
            if hard and not req.meta.get("delivered_in"):
                warnings.append(
                    f"{rel}: status '{status}' but delivered_in is unset "
                    f"(set the app release that delivered it, e.g. delivered_in: v0.10.0)"
                )

    # Change-request impact links: every impacts entry that looks like a
    # requirement UID must resolve to an existing requirement. (Architecture
    # need ids - BLK/QG/RISK - are validated at build time by sphinx-needs.)
    req_uid_re = re.compile(r"^(STK|SYS|SWR)-[A-Z0-9]+-[0-9]{4}$")
    for cr in reqlib.load_change_requests():
        rel = cr.path.relative_to(reqlib.REPO_ROOT)
        for target in cr.meta.get("impacts") or []:
            target = str(target).strip()
            if req_uid_re.match(target) and target not in by_uid:
                findings.append(f"{rel}: impacts '{target}' does not exist")

    if args.matrix_out:
        with open(args.matrix_out, "w", newline="", encoding="utf-8") as fh:
            writer = csv.writer(fh)
            writer.writerow(["uid", "title", "type", "status", "security_classification",
                             "derived_from", "children", "satisfied_by", "verified_by"])
            for req in sorted(reqs, key=lambda r: r.uid):
                writer.writerow([
                    req.uid,
                    req.meta.get("title", ""),
                    req.meta.get("type", ""),
                    req.meta.get("status", ""),
                    req.meta.get("security_classification", ""),
                    "; ".join(req.meta.get("derived_from") or []),
                    "; ".join(sorted(children.get(req.uid, []))),
                    "; ".join(req.meta.get("satisfied_by") or []),
                    "; ".join(req.meta.get("verified_by") or []),
                ])
        print(f"traceability matrix written to {args.matrix_out}")

    if warnings:
        print(f"WARN: {len(warnings)} non-blocking reference warning(s) "
              f"(draft/approved requirements):")
        for w in warnings:
            print(f"  - {w}")

    if findings:
        print(f"FAIL: {len(findings)} traceability finding(s):")
        for f in findings:
            print(f"  - {f}")
        return 1

    print(f"OK: traceability complete for {len(reqs)} requirement(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
