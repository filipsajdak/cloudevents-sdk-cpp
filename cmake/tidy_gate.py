"""The clang-tidy gate: every check on the SDK's headers, the copy checks on the suites.

The library is header-only, so the suites are the translation units that
instantiate it, and they are what clang-tidy is run over. But clang-tidy
always reports findings in the file it was given, whatever
HeaderFilterRegex says, so running it directly would also enforce the
full check set on the test code. This keeps every finding located under
include/cloudevents/, and only the needless-copy findings located under
test/ (D-TIDY-7). It counts each once (a header is linted once per suite
that includes it), and fails if there are any. A suite that does not
parse fails the gate too, because a header it would have covered went
unlinted.

usage: tidy_gate.py <clang-tidy> <build-dir> <source-dir> <suite.cpp>...
"""
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys

clang_tidy, build_dir, source_dir = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
suites = sorted(pathlib.Path(path) for path in sys.argv[4:])
if not suites:
    sys.exit("tidy_gate.py: no suites were registered, so nothing would be linted")
header_root = str(source_dir / "include" / "cloudevents") + os.sep
test_root = str(source_dir / "test") + os.sep
# The checks the suites are gated on (D-TIDY-6, D-TIDY-7), plus every
# query-based one. Each is named in .clang-tidy too, which this confirms, so a
# check dropped from there cannot leave the suites gated on nothing.
copy_checks = {
    "performance-for-range-copy",
    "performance-unnecessary-value-param",
    "performance-unnecessary-copy-initialization",
    "performance-move-const-arg",
    "performance-no-automatic-move",
    "performance-inefficient-string-concatenation",
    "performance-string-view-conversions",
    "modernize-pass-by-value",
    "readability-redundant-string-cstr",
    "bugprone-dangling-handle",
    "bugprone-return-const-ref-from-parameter",
}
copy_prefix = "custom-scudoai-copy-"
configured = (source_dir / ".clang-tidy").read_text()
missing = sorted(check for check in copy_checks if f"  {check}," not in configured)
if missing or "  custom-*\n" not in configured:
    sys.exit(f"tidy_gate.py: .clang-tidy no longer enables {missing or 'custom-*'}")
# The query-based checks in .clang-tidy run only behind this flag. A clang-tidy
# too old to know it would reject every suite with an error the finding pattern
# does not match, and the gate would pass having linted nothing, so it is
# refused up front instead.
custom_checks = ["--experimental-custom-checks"]
probe = subprocess.run([clang_tidy, *custom_checks, "--version"], capture_output=True, text=True)
if probe.returncode != 0:
    sys.exit(f"tidy_gate.py: {clang_tidy} does not accept {custom_checks[0]}; "
             "it predates the query-based checks .clang-tidy defines")
finding = re.compile(r"^(/[^:]+):(\d+):(\d+): (?:warning|error): (.*) \[([\w.,-]+)\]$")


def lint(suite: pathlib.Path) -> tuple[pathlib.Path, int, str]:
    run = subprocess.run([clang_tidy, "-p", build_dir, "--quiet", *custom_checks, str(suite)],
                         capture_output=True, text=True, cwd=source_dir)
    return suite, run.returncode, run.stdout + run.stderr


findings = {}
unparsed = []
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    for suite, _, output in pool.map(lint, suites):
        for line in output.splitlines():
            m = finding.match(line)
            if not m:
                continue
            checks = [c for c in m.group(5).split(",") if not c.startswith("-warnings-as-errors")]
            if "clang-diagnostic-error" in checks:
                unparsed.append(f"{suite.name}: {line}")
            in_headers = m.group(1).startswith(header_root)
            if not in_headers and not m.group(1).startswith(test_root):
                continue
            where = m.group(1)[len(str(source_dir)) + 1:]
            for check in checks:
                if in_headers or check in copy_checks or check.startswith(copy_prefix):
                    findings[(where, int(m.group(2)), int(m.group(3)), check)] = m.group(4)

for (where, line, column, check), message in sorted(findings.items()):
    print(f"{where}:{line}:{column}: {message} [{check}]")
for problem in unparsed:
    print(f"does not parse: {problem}")

print(f"\n{len(findings)} finding(s) in include/cloudevents and test across {len(suites)} suites"
      + (f"; {len(unparsed)} parse failure(s)" if unparsed else ""))
sys.exit(1 if findings or unparsed else 0)
