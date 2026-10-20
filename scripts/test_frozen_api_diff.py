"""Suites for scripts/frozen_api_diff.py.

Registered with CTest as `frozen_api_tools`; standard-library Python only:

    python3 -m unittest discover -s scripts -p 'test_frozen_*.py'

The verdict suites parse a few lines of C++ under a temporary include/ tree and
need a clang++ (CE_FROZEN_API_CLANG, or clang++ on PATH). The release suites
parse this checkout's headers and a tag's, and need in addition the tags
(git fetch --tags) and the headers of the dependencies, which they look for in
CE_FROZEN_API_ISYSTEM; each skips when one is missing.
"""

from __future__ import annotations

import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import frozen_api_diff  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HEADER = Path("include/cloudevents/v3/core.hpp")
MOVED_HEADER = Path("include/cloudevents/v3/moved.hpp")
CLANG = shutil.which(os.environ.get(frozen_api_diff.CLANG_ENV) or frozen_api_diff.DEFAULT_CLANG)
RELEASE_UNDER_V4 = "v0.6.0"
RELEASE_UNDER_V3 = "v0.5.0"
DEPENDENCY_PROBE = "#include <ctre.hpp>\n#include <nlohmann/json.hpp>\n#include <rapidjson/document.h>\n#include <boost/json.hpp>\n"

# One header in the shape of a frozen generation: an enumeration, an aggregate, a
# class with a constructor, a default argument and a body, and a free function.
BASE = """\
#pragma once
namespace ce::v3 {
struct text { text(const char*) {} };
enum class mode { binary = 0, structured = 1 };
struct options { int limit; bool strict = false; };
class widget {
 public:
  explicit widget(int n);
  auto size() const noexcept -> int { return n_; }
  auto name(text prefix = "w") const -> text;
 private:
  int n_;
};
auto make(int n) -> widget { return widget{n}; }
}  // namespace ce::v3
"""


def isystem() -> list[str]:
    return [path for path in os.environ.get(frozen_api_diff.ISYSTEM_ENV, "").split(os.pathsep) if path]


def run(*arguments: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = frozen_api_diff.main(list(arguments))
    return status, out.getvalue(), err.getvalue()


def dependencies_resolve() -> bool:
    if CLANG is None:
        return False
    flags = [f"-isystem{path}" for path in isystem()]
    probe = subprocess.run(
        [CLANG, f"-std={frozen_api_diff.DEFAULT_STANDARD}", "-fsyntax-only", "-x", "c++", "-", *flags],
        input=DEPENDENCY_PROBE, capture_output=True, text=True, check=False,
    )
    return probe.returncode == 0


def tag_exists(tag: str) -> bool:
    found = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--verify", "--quiet", f"{tag}^{{commit}}"], capture_output=True, check=False)
    return found.returncode == 0


# spec: SWR-BUILD-0015
@unittest.skipUnless(CLANG, "frozen_api_diff.py needs clang++ (CE_FROZEN_API_CLANG)")
class Verdicts(unittest.TestCase):
    """A seeded change to a v3 header, judged against the same header unchanged."""

    def setUp(self) -> None:
        self.scratch = tempfile.TemporaryDirectory(prefix="frozen_api_test.")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name) / "base"
        self.head = Path(self.scratch.name) / "head"
        self.write(self.base, HEADER, BASE)
        self.write(self.head, HEADER, BASE)

    def write(self, root: Path, header: Path, text: str) -> None:
        (root / header).parent.mkdir(parents=True, exist_ok=True)
        (root / header).write_text(text, encoding="ascii")

    def verdict(self, changed: str) -> tuple[int, str]:
        """Exit status and report for the base header against the changed head header."""
        self.write(self.head, HEADER, changed)
        status, out, _ = run("--base-dir", str(self.base), "--head-dir", str(self.head))
        return status, out

    def test_an_unchanged_header_passes(self) -> None:
        self.assertEqual(self.verdict(BASE)[0], frozen_api_diff.EXIT_OK)

    def test_a_changed_return_type_fails_and_names_the_declaration(self) -> None:
        status, out = self.verdict(BASE.replace("auto make(int n) -> widget", "auto make(int n) -> int").replace("return widget{n};", "return n;"))
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)
        self.assertIn("ce::v3::make", out)

    def test_a_changed_function_body_passes(self) -> None:
        status, out = self.verdict(BASE.replace("return n_; }", "return n_ + 0; }").replace("return widget{n};", "auto made = widget{n}; return made;"))
        self.assertEqual((status, out.count("changed:")), (frozen_api_diff.EXIT_OK, 0))

    def test_an_added_declaration_passes(self) -> None:
        added = BASE.replace(
            "enum class mode { binary = 0, structured = 1 };",
            "enum class mode { binary = 0, structured = 1, batch = 2 };",
        ).replace("}  // namespace ce::v3", "auto made_twice(int n) -> widget;\nstruct added { int x; };\n}  // namespace ce::v3")
        self.assertEqual(self.verdict(added)[0], frozen_api_diff.EXIT_OK)

    def test_a_member_appended_to_an_aggregate_without_a_default_initialiser_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("bool strict = false; };", "bool strict = false; int extra; };"))[0], frozen_api_diff.EXIT_BROKEN)

    def test_aggregate_members_reordered_removed_or_retyped_fail(self) -> None:
        for name, text in {
            "reordered": BASE.replace("int limit; bool strict = false;", "bool strict = false; int limit;"),
            "removed": BASE.replace("int limit; bool strict = false;", "bool strict = false;"),
            "retyped": BASE.replace("int limit;", "long limit;"),
        }.items():
            with self.subTest(name):
                self.assertEqual(self.verdict(text)[0], frozen_api_diff.EXIT_BROKEN)

    def test_a_member_appended_to_an_aggregate_with_a_default_initialiser_passes(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("bool strict = false; };", "bool strict = false; int extra = 0; };"))[0], frozen_api_diff.EXIT_OK)

    def test_a_removed_declaration_fails_and_names_it(self) -> None:
        status, out = self.verdict(BASE.replace("  auto size() const noexcept -> int { return n_; }\n", ""))
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)
        self.assertIn("removed: ce::v3::widget::size", out)

    def test_a_changed_enumerator_value_fails(self) -> None:
        status, out = self.verdict(BASE.replace("structured = 1", "structured = 2"))
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)
        self.assertIn("ce::v3::mode::structured", out)

    def test_a_dropped_explicit_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("explicit widget(int n);", "widget(int n);"))[0], frozen_api_diff.EXIT_BROKEN)

    def test_a_changed_default_argument_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace('prefix = "w"', 'prefix = "x"'))[0], frozen_api_diff.EXIT_BROKEN)

    def test_a_changed_default_member_initialiser_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("strict = false", "strict = true"))[0], frozen_api_diff.EXIT_BROKEN)

    def test_a_field_inserted_before_another_in_an_aggregate_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("int limit; bool strict", "int limit; int extra; bool strict"))[0], frozen_api_diff.EXIT_BROKEN)

    def test_a_changed_access_fails(self) -> None:
        status, _ = self.verdict(BASE.replace("auto name(text", "auto name_(text").replace(" private:", " private:\n  auto name(text prefix = \"w\") const -> text;"))
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)

    def test_a_private_member_is_not_a_declaration_a_consumer_can_use(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("  int n_;", "  long n_;\n  int spare_;"))[0], frozen_api_diff.EXIT_OK)

    def test_a_hidden_friend_declared_in_a_private_section_is_still_found_by_lookup(self) -> None:
        friend = "  friend auto operator==(const widget&, const widget&) -> bool { return true; }\n"
        with_friend = BASE.replace(" private:\n", " private:\n" + friend)
        self.write(self.base, HEADER, with_friend)
        self.assertEqual(self.verdict(with_friend)[0], frozen_api_diff.EXIT_OK)
        status, out = self.verdict(BASE)
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)
        self.assertIn("friend", out)

    def test_a_removed_noexcept_fails(self) -> None:
        self.assertEqual(self.verdict(BASE.replace("const noexcept -> int", "const -> int"))[0], frozen_api_diff.EXIT_BROKEN)

    def test_comments_and_parameter_names_are_not_declarations(self) -> None:
        text = BASE.replace("auto make(int n) -> widget { return widget{n}; }", "// reworded\nauto make(int count) -> widget { return widget{count}; }")
        self.assertEqual(self.verdict(text)[0], frozen_api_diff.EXIT_OK)

    def test_a_declaration_moved_to_another_header_passes(self) -> None:
        moved = "#pragma once\nnamespace ce::v3 {\nenum class mode { binary = 0, structured = 1 };\n}\n"
        self.write(self.head, MOVED_HEADER, moved)
        self.write(self.head, HEADER, BASE.replace("enum class mode { binary = 0, structured = 1 };\n", "").replace("namespace ce::v3 {", '#include "moved.hpp"\nnamespace ce::v3 {', 1))
        status, out, _ = run("--base-dir", str(self.base), "--head-dir", str(self.head))
        self.assertEqual((status, out.count("removed:")), (frozen_api_diff.EXIT_OK, 0))

    def test_a_generation_the_base_lacks_is_not_frozen(self) -> None:
        self.write(self.base, HEADER, BASE + "namespace ce::v4 { auto brand_new(int n) -> int; }\n")
        self.write(self.head, HEADER, BASE + "namespace ce::v4 { auto brand_new(int n) -> int; }\n")
        status, _, _ = run("--base-dir", str(self.base), "--head-dir", str(self.head))
        self.assertEqual(status, frozen_api_diff.EXIT_OK)
        self.write(self.base, HEADER, BASE)
        self.assertEqual(run("--base-dir", str(self.base), "--head-dir", str(self.head))[0], frozen_api_diff.EXIT_OK)

    def test_a_base_that_is_not_a_tree_is_a_usage_error_not_a_pass(self) -> None:
        status, _, err = run("--base-dir", str(self.base / "missing"), "--head-dir", str(self.head))
        self.assertEqual(status, frozen_api_diff.EXIT_UNUSABLE)
        self.assertIn("no .hpp header", err)

    def test_a_header_that_does_not_parse_is_a_usage_error_not_a_pass(self) -> None:
        self.write(self.head, HEADER, "namespace ce::v3 { this is not C++ }\n")
        self.assertEqual(run("--base-dir", str(self.base), "--head-dir", str(self.head))[0], frozen_api_diff.EXIT_UNUSABLE)

    def test_a_compiler_that_is_not_there_is_a_usage_error(self) -> None:
        status, _, err = run("--clang", "no-such-clang++", "--base-dir", str(self.base), "--head-dir", str(self.head))
        self.assertEqual(status, frozen_api_diff.EXIT_UNUSABLE)
        self.assertIn("no-such-clang++ not found", err)


# spec: SWR-BUILD-0015
@unittest.skipUnless(CLANG and dependencies_resolve(), "needs clang++ and the dependency headers (CE_FROZEN_API_ISYSTEM)")
class AgainstReleases(unittest.TestCase):
    """The real headers, against the tags that froze them."""

    def check(self, tag: str) -> tuple[int, str]:
        if not tag_exists(tag):
            self.skipTest(f"{tag} is not in this clone (git fetch --tags)")
        status, out, _ = run("--base", tag)
        return status, out

    def test_the_headers_keep_every_declaration_v0_6_0_published(self) -> None:
        status, out = self.check(RELEASE_UNDER_V4)
        self.assertEqual((status, out), (frozen_api_diff.EXIT_OK, "frozen_api_diff: no frozen declaration removed or changed since v0.6.0\n"))

    def test_the_headers_keep_every_v1_v2_and_v3_declaration_v0_5_0_published(self) -> None:
        status, out = self.check(RELEASE_UNDER_V3)
        self.assertEqual((status, out), (frozen_api_diff.EXIT_OK, "frozen_api_diff: no frozen declaration removed or changed since v0.5.0\n"))

    def test_a_seeded_change_to_a_v3_declaration_fails(self) -> None:
        if not tag_exists(RELEASE_UNDER_V4):
            self.skipTest(f"{RELEASE_UNDER_V4} is not in this clone (git fetch --tags)")
        with tempfile.TemporaryDirectory(prefix="frozen_api_seed.") as scratch:
            seeded = Path(scratch)
            shutil.copytree(REPO / "include", seeded / "include")
            core = seeded / "include/cloudevents/v3/core.hpp"
            text = core.read_text(encoding="utf-8")
            marker = "namespace ce::v3 {\n"
            self.assertIn(marker, text)
            core.write_text(text.replace(marker, marker + "auto seeded_probe(int n) -> int;\n", 1), encoding="utf-8")
            self.assertEqual(run("--base", RELEASE_UNDER_V4, "--head-dir", str(seeded))[0], frozen_api_diff.EXIT_OK, "an addition passes")
            core.write_text(text, encoding="utf-8")
            changed = text.replace("class event {", "struct event {", 1)
            self.assertNotEqual(changed, text, "ce::v3::event is declared in v3/core.hpp")
            core.write_text(changed, encoding="utf-8")
            status, out, _ = run("--base", RELEASE_UNDER_V4, "--head-dir", str(seeded))
        self.assertEqual(status, frozen_api_diff.EXIT_BROKEN)
        self.assertIn("ce::v3::event", out)


if __name__ == "__main__":
    unittest.main()
