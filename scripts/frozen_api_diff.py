#!/usr/bin/env python3
"""Fail when a frozen declaration was removed or changed since a release tag.

    frozen_api_diff.py [--base TAG | --base-dir DIR] [--head-dir DIR]
                       [--clang CLANG] [--isystem DIR]... [--cxxflag FLAG]...

SWR-BUILD-0015, SWR-BUILD-0006. A generation (ce::v1 to ce::v4) is frozen once a
tag published it: a later change may add to it and fix a body, and may not
remove or change a declaration. This script compares the public declarations of
the headers at the base with the headers in the head tree.

Method. Every header under include/cloudevents/ is included from one generated
translation unit, which clang parses to a JSON syntax tree (-ast-dump=json).
The tree's declarations in namespace ce are reduced to one signature each
(kind, qualified name, type, access, template parameters, base classes,
enumerator values, default arguments, default member initialisers, attributes,
explicit, aggregate-ness, and the order of a class's fields), and the two
revisions' signatures are compared by name. Function bodies, constructor
initialiser lists, comments, parameter names and private members are left out,
so a defect fix passes; an added declaration passes; a removed or a changed
one fails.
A text diff cannot tell those apart, and a per-file diff cannot follow an entity
that moved between headers, as the v3 entities did when v0.6.0 moved them into
include/cloudevents/v3/.

Checked: every .hpp under include/cloudevents/, so the headers under v1/, v2/ and
v3/ and the shared headers that declare into those generations (attributes,
message, result, describe, extensions, detail/, codec/, format/, binding/ and
the rest). Not checked: EXCLUDED_HEADERS, and the module wrapper cloudevents.cppm.

A generation is frozen at the base when the base declares it, so a base of
v0.5.0 freezes v1 to v3 and a base of v0.6.0 freezes v1 to v4.

Exit 0: nothing frozen was removed or changed. Exit 1: at least one was. Exit 2:
the check could not run (no clang, unknown base, a header that does not parse).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

EXIT_OK = 0
EXIT_BROKEN = 1
EXIT_UNUSABLE = 2

REPO = Path(__file__).resolve().parent.parent
HEADER_ROOT = "include"
HEADER_DIR = "cloudevents"
HEADER_SUFFIX = ".hpp"

# Headers that are not parsed, relative to include/. The reflection backend needs
# a compiler with C++26 static reflection (only GCC 16 has it, behind
# -freflection) and declares nothing the macro backend in describe_macro.hpp does
# not also declare for the same ce::vN::detail names.
EXCLUDED_HEADERS = frozenset({"cloudevents/detail/describe_reflection.hpp"})

ROOT_NAMESPACE = "ce"
GENERATION_RE = re.compile(r"^v[0-9]+$")
DEFAULT_STANDARD = "c++20"
CLANG_ENV = "CE_FROZEN_API_CLANG"
ISYSTEM_ENV = "CE_FROZEN_API_ISYSTEM"
DEFAULT_CLANG = "clang++"
TRANSLATION_UNIT = "frozen_api_tu.cpp"

# Keys that name a position or an address in the tree, not a property of the
# declaration. A pointer-looking string is an address too.
NOISE_KEYS = frozenset({
    "id", "loc", "range", "previousDecl", "parentDeclContextId", "isUsed",
    "isReferenced", "mangledName", "isImplicit", "inline", "nrvo", "begin", "end",
})
ADDRESS_RE = re.compile(r"^0x[0-9a-f]+$")
# The name clang gives a lambda or an unnamed type carries its source position,
# which moves whenever a line is added above it.
UNNAMED_RE = re.compile(r"\((lambda|unnamed (?:struct|class|union|enum)|anonymous (?:struct|class|union|enum)) at [^)]*\)")
EXPLICIT_RE = re.compile(r"\bexplicit\b(\s*\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\))?")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

TEMPLATE_PARAMETER_KINDS = frozenset({
    "TemplateTypeParmDecl", "NonTypeTemplateParmDecl", "TemplateTemplateParmDecl",
})
RECORD_KINDS = frozenset({
    "CXXRecordDecl", "ClassTemplateSpecializationDecl", "ClassTemplatePartialSpecializationDecl",
})
FUNCTION_KINDS = frozenset({
    "FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl",
    "CXXConversionDecl", "CXXDeductionGuideDecl",
})
EXPLICIT_KINDS = frozenset({"CXXConstructorDecl", "CXXConversionDecl", "CXXDeductionGuideDecl"})
VARIABLE_KINDS = frozenset({"VarDecl", "VarTemplateSpecializationDecl", "VarTemplatePartialSpecializationDecl"})
TEMPLATE_KINDS = frozenset({
    "ClassTemplateDecl", "FunctionTemplateDecl", "VarTemplateDecl", "TypeAliasTemplateDecl",
})
BODY_KINDS = frozenset({"CompoundStmt", "CXXCtorInitializer", "FullComment", "CXXTryStmt"})
ALIAS_KINDS = frozenset({"TypeAliasDecl", "TypedefDecl"})


class Unusable(Exception):
    """The check could not run; the message says why."""


# The generation whose namespace is inline in the tree being read. Clang prints a
# name from an inline namespace without it (ce::event for ce::v3::event while v3
# is inline), so the same entity is spelled ce::v3::event once v4 is inline.
# spell() writes the generation out, which makes the two spellings equal.
INLINE = {"generation": None}
QUALIFIED_RE = re.compile(r"\bce(?:::\w+)+")
ALIAS_DEPTH = 8
# Signature entries that are an ordered list a later release may extend at the end.
APPENDABLE = ("fields", "order")
UNQUALIFIED_RE = re.compile(r"\bce::(?!v[0-9]+::)")


def spell(text: str) -> str:
    text = UNNAMED_RE.sub(lambda found: f"({found.group(1)})", text)
    if INLINE["generation"]:
        text = UNQUALIFIED_RE.sub(f"ce::{INLINE['generation']}::", text)
    return text


def strip(node):
    """The node without addresses and positions, and each type as the type it names.

    A type is its desugared spelling, so a name that resolves to the same
    entity compares equal however it was spelled (ce::result is ce::v3::result
    while v3 is inline and ce::v4::result after). A node that is a type loses its
    children, which only restate the spelling.
    """
    if isinstance(node, dict):
        if "qualType" in node:
            return spell(node.get("desugaredQualType") or node["qualType"])
        if node.get("kind", "").endswith("Type") and "type" in node:
            return {"kind": node["kind"], "type": strip(node["type"])}
        return {
            key: strip(value) for key, value in node.items()
            if key not in NOISE_KEYS and not (isinstance(value, str) and ADDRESS_RE.match(value))
        }
    if isinstance(node, list):
        return [strip(item) for item in node if not item.get("kind", "").endswith("Comment")] if all(isinstance(i, dict) for i in node) else [strip(item) for item in node]
    if isinstance(node, str):
        return spell(node)
    return node


def type_of(node) -> str:
    kind = node.get("type")
    return strip(kind) if isinstance(kind, dict) and "qualType" in kind else ""


def fill_files(node, state: dict) -> None:
    """Restore the file name clang leaves out of a location equal to the previous one."""
    if isinstance(node, dict):
        if "offset" in node:
            if "file" in node:
                state["file"] = node["file"]
            else:
                node["file"] = state.get("file")
        for value in node.values():
            fill_files(value, state)
    elif isinstance(node, list):
        for item in node:
            fill_files(item, state)


def explicit_of(node, sources: dict) -> str:
    """The `explicit` specifier written before a constructor's or conversion's name.

    Clang's JSON tree has no field for it, so the text between the start of the
    declaration and its name is read from the header.
    """
    begin = node.get("range", {}).get("begin", {})
    name = node.get("loc", {})
    if "offset" not in begin or "offset" not in name or not begin.get("file") or begin["file"] != name.get("file"):
        return ""
    path = begin["file"]
    if path not in sources:
        sources[path] = Path(path).read_bytes()
    text = sources[path][begin["offset"]:name["offset"]].decode("utf-8", "replace")
    found = EXPLICIT_RE.search(COMMENT_RE.sub(" ", text))
    return " ".join(found.group(0).split()) if found else ""


def attributes_of(node) -> list:
    return [strip(item) for item in node.get("inner", []) if item.get("kind", "").endswith("Attr") and not item.get("isImplicit")]


def parameters_of(node) -> list:
    """Parameter types and default arguments; the names are not part of the declaration."""
    result = []
    for item in node.get("inner", []):
        if item.get("kind") == "ParmVarDecl":
            parameter = strip({**item, "inner": [c for c in item.get("inner", []) if not c.get("kind", "").endswith("Attr")]})
            parameter.pop("name", None)
            result.append(parameter)
    return result


def trailing_of(node) -> list:
    """Anything after the parameters that is not a body: a requires-clause, say."""
    result = []
    for item in node.get("inner", []):
        kind = item.get("kind", "")
        if kind == "ParmVarDecl" or kind in BODY_KINDS or kind.endswith("Attr"):
            continue
        result.append(strip(item))
    return result


def shape(parameters: list) -> str:
    """What identifies an overload among templates: the parameter kinds, not their constraints."""
    return "<" + ",".join(f"{p.get('kind', '')}{'...' if p.get('isParameterPack') else ''}" for p in parameters) + ">" if parameters else ""


class Collector:
    """Declarations by key, from the roots clang dumped for namespace ce."""

    def __init__(self) -> None:
        self.found: dict[str, dict] = {}
        self.sources: dict = {}
        # A using-declaration names an entity again under another scope:
        # ce::v4::result is ce::v1::result, so both spell one type.
        self.aliases: dict[str, str] = {}
        # The same for a namespace alias (namespace codec = ce::v1::codec), which
        # names its target by the id of a declaration of it.
        self.namespaces: dict[str, str] = {}
        self.namespace_aliases: list[tuple[str, str]] = []

    def add(self, key: str, signature: dict) -> None:
        unique = key
        ordinal = 1
        while unique in self.found:
            ordinal += 1
            unique = f"{key} #{ordinal}"
        self.found[unique] = signature

    def canonical(self, text: str) -> str:
        """The text with every ce:: name that is a using-declaration replaced by what it names."""
        def resolve(found: re.Match) -> str:
            parts = found.group(0).split("::")
            for length in range(len(parts), 1, -1):
                target = self.aliases.get("::".join(parts[:length]))
                if target:
                    return "::".join([target, *parts[length:]])
            return found.group(0)

        for _ in range(ALIAS_DEPTH):
            replaced = QUALIFIED_RE.sub(resolve, text)
            if replaced == text:
                break
            text = replaced
        return text

    def canonicalise(self) -> dict[str, dict]:
        """The declarations with every type spelled through its using-declarations."""
        def walk(value):
            if isinstance(value, dict):
                return {key: walk(item) for key, item in value.items()}
            if isinstance(value, list):
                return [walk(item) for item in value]
            return self.canonical(value) if isinstance(value, str) else value

        for alias, target in self.namespace_aliases:
            if target in self.namespaces:
                self.aliases[alias] = self.namespaces[target]
        result = {}
        for key, signature in self.found.items():
            head, bracket, tail = key.partition(" [")
            result[head + bracket + self.canonical(tail)] = walk(signature)
        return result

    def namespace(self, scope: str, node: dict) -> None:
        self.members(scope, node.get("inner", []), None)

    def members(self, scope: str, children: list, access: str | None) -> None:
        for child in children:
            if "kind" not in child:
                continue
            kind = child["kind"]
            if kind == "AccessSpecDecl":
                access = child.get("access", access)
                continue
            if child.get("isImplicit") and kind != "UsingShadowDecl":
                continue
            if access == "private":
                continue
            self.member(scope, child, access)

    def member(self, scope: str, child: dict, access: str | None) -> None:
        kind = child["kind"]
        name = child.get("name", "")
        if kind == "NamespaceDecl":
            self.namespaces[child.get("id", "")] = f"{scope}::{name or '(anonymous)'}"
            self.namespace(f"{scope}::{name or '(anonymous)'}", child)
        elif kind == "NamespaceAliasDecl":
            self.add(f"{scope}::{name} [NamespaceAliasDecl]", {})
            self.namespace_aliases.append((f"{scope}::{name}", child.get("aliasedNamespace", {}).get("id", "")))
        elif kind in RECORD_KINDS:
            self.record(scope, child, access, [])
        elif kind in TEMPLATE_KINDS:
            self.template(scope, child, access)
        elif kind in FUNCTION_KINDS:
            self.function(scope, child, access, [])
        elif kind in VARIABLE_KINDS:
            self.variable(scope, child, access, [])
        elif kind == "FieldDecl":
            self.add(f"{scope}::{name} [FieldDecl]", {
                "access": access, "type": type_of(child), "mutable": bool(child.get("mutable")),
                "initialiser": strip([c for c in child.get("inner", []) if not c.get("kind", "").endswith("Attr")]),
            })
        elif kind == "EnumDecl":
            self.enumeration(scope, child, access)
        elif kind in ALIAS_KINDS:
            self.add(f"{scope}::{name} [{kind}]", {"type": type_of(child), "access": access, "target": ""})
        elif kind == "ConceptDecl":
            self.add(f"{scope}::{name} [concept]", {"parts": strip(child.get("inner", []))})
        elif kind == "FriendDecl":
            self.friend(scope, child)
        elif kind == "UsingDecl":
            self.add(f"{scope}::{name} [using]", {"access": access})
            written = spell(name)
            if written.startswith(f"{ROOT_NAMESPACE}::"):
                self.aliases[f"{scope}::{name.rsplit('::', 1)[-1]}"] = written
        elif kind == "UsingShadowDecl":
            target = child.get("target", {})
            self.add(f"{scope}::{target.get('name', '')} [using-target {target.get('kind', '')}] {type_of(target)}", {"access": access})
        elif kind in ("UsingDirectiveDecl", "UsingEnumDecl"):
            self.add(f"{scope} [{kind}] {name}", strip(child))

    def template(self, scope: str, node: dict, access: str | None) -> None:
        children = node.get("inner", [])
        parameters = [strip(c) for c in children if c.get("kind") in TEMPLATE_PARAMETER_KINDS]
        for parameter in parameters:
            parameter.pop("name", None)
        templated = next((c for c in children if c.get("kind") not in TEMPLATE_PARAMETER_KINDS and "kind" in c), None)
        if templated is None:
            return
        kind = templated["kind"]
        if kind in RECORD_KINDS:
            self.record(scope, templated, access, parameters)
        elif kind in FUNCTION_KINDS:
            self.function(scope, templated, access, parameters)
        elif kind in VARIABLE_KINDS:
            self.variable(scope, templated, access, parameters)
        elif kind in ALIAS_KINDS:
            self.add(f"{scope}::{node.get('name', '')} [alias template {shape(parameters)}]", {"type": type_of(templated), "access": access, "target": ""})

    def record(self, scope: str, node: dict, access: str | None, parameters: list) -> None:
        if not node.get("completeDefinition"):
            return
        name = node.get("name", "")
        tag = node.get("tagUsed", "struct")
        arguments = [strip(c) for c in node.get("inner", []) if c.get("kind") == "TemplateArgument"]
        key = f"{scope}::{name} [{node['kind']} {shape(parameters)} {arguments}]"
        children = node.get("inner", [])
        data = node.get("definitionData", {})
        fields = self.visible_fields(children, "private" if tag == "class" else "public")
        self.add(key, {
            "tag": tag, "access": access, "bases": strip(node.get("bases", [])),
            "aggregate": bool(data.get("isAggregate")), "fields": fields, "parameters": parameters,
            "attributes": attributes_of(node),
        })
        inside = [c for c in children if c.get("kind") not in TEMPLATE_PARAMETER_KINDS | {"TemplateArgument"}]
        self.members(f"{scope}::{name}", inside, "private" if tag == "class" else "public")

    @staticmethod
    def visible_fields(children: list, access: str) -> list[str]:
        """The names of the fields a consumer can reach, in declaration order."""
        names = []
        for child in children:
            if child.get("kind") == "AccessSpecDecl":
                access = child.get("access", access)
            elif child.get("kind") == "FieldDecl" and not child.get("isImplicit") and access != "private":
                names.append(child.get("name", ""))
        return names

    def function(self, scope: str, node: dict, access: str | None, parameters: list) -> None:
        if "previousDecl" in node:
            return
        name = node.get("name", "")
        defaulted = bool(node.get("explicitlyDefaulted"))
        signature = {
            "access": access, "type": type_of(node).removesuffix(" noexcept") if defaulted else type_of(node),
            "parameters": parameters,
            "storage": node.get("storageClass", ""), "constexpr": bool(node.get("constexpr")) and not defaulted,
            "immediate": bool(node.get("immediate")), "virtual": bool(node.get("virtual")),
            "pure": bool(node.get("pure")), "deleted": bool(node.get("explicitlyDeleted")),
            "defaulted": node.get("explicitlyDefaulted", ""), "attributes": attributes_of(node),
            "arguments": parameters_of(node), "trailing": trailing_of(node),
        }
        if node["kind"] in EXPLICIT_KINDS:
            signature["explicit"] = explicit_of(node, self.sources)
        self.add(f"{scope}::{name} [{node['kind']}] {signature['type']} {shape(parameters)}", signature)

    def variable(self, scope: str, node: dict, access: str | None, parameters: list) -> None:
        if "previousDecl" in node:
            return
        self.add(f"{scope}::{node.get('name', '')} [{node['kind']}] {shape(parameters)}", {
            "access": access, "type": type_of(node), "storage": node.get("storageClass", ""),
            "constexpr": bool(node.get("constexpr")), "init": strip(node.get("inner", [])),
            "attributes": attributes_of(node),
        })

    def enumeration(self, scope: str, node: dict, access: str | None) -> None:
        constants = [c for c in node.get("inner", []) if c.get("kind") == "EnumConstantDecl"]
        if not constants and not node.get("completeDefinition", True):
            return
        name = node.get("name", "")
        self.add(f"{scope}::{name} [EnumDecl]", {
            "access": access, "scoped": node.get("scopedEnumTag", ""),
            "underlying": strip(node.get("fixedUnderlyingType", {})), "order": [c.get("name", "") for c in constants],
        })
        for constant in constants:
            self.add(f"{scope}::{name}::{constant.get('name', '')} [EnumConstantDecl]", {"value": strip(constant.get("inner", []))})

    def friend(self, scope: str, node: dict) -> None:
        for child in node.get("inner", []):
            kind = child.get("kind", "")
            if kind in FUNCTION_KINDS:
                self.function(f"{scope} friend", child, None, [])
            elif kind == "FunctionTemplateDecl":
                self.template(f"{scope} friend", child, None)
            elif kind in ("CXXRecordDecl", "ClassTemplateDecl"):
                self.add(f"{scope} friend [{kind}] {child.get('name', '')}", {})
        if node.get("type"):
            self.add(f"{scope} friend [type] {type_of(node)}", {})


def header_paths(root: Path) -> list[str]:
    base = root / HEADER_ROOT
    found = sorted(
        path.relative_to(base).as_posix()
        for path in (base / HEADER_DIR).rglob(f"*{HEADER_SUFFIX}")
    )
    return [path for path in found if path not in EXCLUDED_HEADERS]


def dump(root: Path, clang: str, isystem: list[str], flags: list[str], work: Path) -> dict[str, dict]:
    """The declarations of the headers under root/include, by key."""
    headers = header_paths(root)
    if not headers:
        raise Unusable(f"no {HEADER_SUFFIX} header under {root / HEADER_ROOT / HEADER_DIR}")
    unit = work / TRANSLATION_UNIT
    unit.write_text("".join(f"#include <{path}>\n" for path in headers), encoding="ascii")
    command = [
        clang, f"-std={DEFAULT_STANDARD}", "-fsyntax-only", f"-I{(root / HEADER_ROOT).resolve()}",
        *[f"-isystem{path}" for path in isystem], *flags,
        "-Xclang", "-ast-dump=json", "-Xclang", f"-ast-dump-filter={ROOT_NAMESPACE}", str(unit),
    ]
    done = subprocess.run(command, capture_output=True, text=True, errors="replace", check=False)
    if done.returncode != 0:
        raise Unusable(f"{clang} could not parse the headers of {root}:\n{done.stderr[-2000:]}")
    return collect(done.stdout)


ROOT_START_RE = re.compile(r"^\{\n", re.M)
# The first "name" in a dumped root is the root's own: a location holds none.
ROOT_NAME_RE = re.compile(r'"kind": "NamespaceDecl",.*?"name": "(\w+)"', re.S)
ROOT_PEEK = 4000


def collect(text: str) -> dict[str, dict]:
    """Fold the dumped roots (one JSON document each) into declarations by key.

    The filter matches every declaration whose name contains "ce", the standard
    library's among them, and most of the output is theirs; a root is parsed
    only when it is namespace ce.
    """
    roots = []
    decoder = json.JSONDecoder()
    for found in ROOT_START_RE.finditer(text):
        peek = ROOT_NAME_RE.search(text, found.start(), found.start() + ROOT_PEEK)
        if peek is not None and peek.group(1) == ROOT_NAMESPACE:
            node, _ = decoder.raw_decode(text, found.start())
            fill_files(node, {})
            roots.append(node)
    INLINE["generation"] = None
    for root in roots:
        for child in root.get("inner", []):
            if child.get("kind") == "NamespaceDecl" and child.get("isInline") and GENERATION_RE.match(child.get("name", "")):
                INLINE["generation"] = child["name"]
    collector = Collector()
    for root in roots:
        collector.namespace(ROOT_NAMESPACE, root)
    return collector.canonicalise()


def generation_of(key: str) -> str | None:
    parts = key.split("::")
    if len(parts) > 2 and parts[0] == ROOT_NAMESPACE and GENERATION_RE.match(parts[1]):
        return parts[1]
    return None


def kept(was: dict, now: dict) -> dict:
    """The head's signature, with fields it appended to a class left out.

    A field added after the last one leaves every initialiser and every access a
    consumer wrote working. One inserted before another moves them, and a removed
    one is gone; both stay in the signature and fail.
    """
    result = dict(now)
    for ordered in APPENDABLE:
        if isinstance(was.get(ordered), list) and isinstance(now.get(ordered), list):
            count = len(was[ordered])
            if now[ordered][:count] == was[ordered]:
                result[ordered] = was[ordered]
    return result


def compare(base: dict[str, dict], head: dict[str, dict]) -> list[tuple[str, str, object, object]]:
    """Removed and changed declarations of every generation the base declares."""
    frozen = {generation_of(key) for key in base} - {None}
    problems = []
    for key, was in sorted(base.items()):
        if generation_of(key) not in frozen:
            continue
        if key not in head:
            problems.append(("removed", key, was, None))
        elif kept(was, head[key]) != was:
            problems.append(("changed", key, was, kept(was, head[key])))
    return problems


def describe(was, now) -> list[str]:
    if not isinstance(was, dict) or not isinstance(now, dict):
        return []
    lines = []
    for field in sorted(set(was) | set(now)):
        if was.get(field) != now.get(field):
            lines.append(f"    {field}: {json.dumps(was.get(field), sort_keys=True)[:300]}  ->  {json.dumps(now.get(field), sort_keys=True)[:300]}")
    return lines


def find_clang(requested: str | None) -> str:
    name = requested or os.environ.get(CLANG_ENV) or DEFAULT_CLANG
    found = shutil.which(name)
    if not found:
        raise Unusable(f"{name} not found; install clang or pass --clang (or set {CLANG_ENV})")
    return found


def git(args: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, check=False, **kwargs)


def latest_tag() -> str:
    done = git(["tag", "--list", "v*", "--sort=-version:refname"], text=True)
    tags = done.stdout.split()
    if done.returncode != 0 or not tags:
        raise Unusable("no v* tag in this clone; fetch tags (git fetch --tags) or pass --base")
    return tags[0]


def export_tag(tag: str, into: Path) -> None:
    if git(["rev-parse", "--verify", "--quiet", f"{tag}^{{commit}}"]).returncode != 0:
        raise Unusable(f"{tag} is not a commit in this clone; fetch tags (git fetch --tags)")
    archive = git(["archive", tag, HEADER_ROOT])
    if archive.returncode != 0:
        raise Unusable(f"git archive {tag} failed: {archive.stderr.decode(errors='replace')}")
    unpacked = subprocess.run(["tar", "-x", "-C", str(into)], input=archive.stdout, capture_output=True, check=False)
    if unpacked.returncode != 0:
        raise Unusable(f"tar failed: {unpacked.stderr.decode(errors='replace')}")


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--base", help="release tag to compare with (default: the latest v* tag by version sort)")
    where.add_argument("--base-dir", type=Path, help="a directory holding include/ to compare with, instead of a tag")
    parser.add_argument("--head-dir", type=Path, default=REPO, help="the tree holding include/ to check (default: this checkout)")
    parser.add_argument("--clang", help=f"clang++ to parse with (default: ${CLANG_ENV}, then {DEFAULT_CLANG})")
    parser.add_argument("--isystem", action="append", default=[], help=f"dependency include directory (also ${ISYSTEM_ENV}, {os.pathsep}-separated)")
    parser.add_argument("--cxxflag", action="append", default=[], help="extra compiler flag, for example --gcc-install-dir=...")
    return parser.parse_args(argv)


def run(args: argparse.Namespace) -> int:
    clang = find_clang(args.clang)
    isystem = [*args.isystem, *[p for p in os.environ.get(ISYSTEM_ENV, "").split(os.pathsep) if p]]
    with tempfile.TemporaryDirectory(prefix="frozen_api_diff.") as scratch:
        work = Path(scratch)
        if args.base_dir:
            base_root, label = args.base_dir, str(args.base_dir)
        else:
            tag = args.base or latest_tag()
            base_root, label = work / "base", tag
            base_root.mkdir()
            export_tag(tag, base_root)
        base = dump(base_root, clang, isystem, args.cxxflag, work)
        head = dump(args.head_dir, clang, isystem, args.cxxflag, work)
    problems = compare(base, head)
    generations = sorted({generation_of(k) for k in base} - {None})
    print(f"frozen_api_diff: {len(base)} declarations of {label} ({', '.join(f'ce::{g}' for g in generations)}) compared with {args.head_dir}", file=sys.stderr)
    for verdict, key, was, now in problems:
        print(f"{verdict}: {key}")
        for line in describe(was, now):
            print(line)
    if problems:
        print(f"frozen_api_diff: {len(problems)} frozen declaration(s) removed or changed since {label}; a break belongs in a new generation (SWR-BUILD-0006)")
        return EXIT_BROKEN
    print(f"frozen_api_diff: no frozen declaration removed or changed since {label}")
    return EXIT_OK


def main(argv: list[str]) -> int:
    args = parse_arguments(argv)
    try:
        return run(args)
    except Unusable as problem:
        print(f"frozen_api_diff: {problem}", file=sys.stderr)
        return EXIT_UNUSABLE


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
