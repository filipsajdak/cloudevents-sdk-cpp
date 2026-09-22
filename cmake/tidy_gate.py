"""The clang-tidy gate: fail on any finding in the SDK's own headers.

The library is header-only, so the suites are the translation units that
instantiate it, and they are what clang-tidy is run over. But clang-tidy
always reports findings in the file it was given, whatever
HeaderFilterRegex says, so running it directly would also enforce the
full check set on the test code. The gate is about the headers: this
keeps the findings located under include/cloudevents/, counts each once
(a header is linted once per suite that includes it), and fails if there
are any. A suite that does not parse fails the gate too, because a header
it would have covered went unlinted.

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
finding = re.compile(r"^(/[^:]+):(\d+):(\d+): (?:warning|error): (.*) \[([\w.,-]+)\]$")


def lint(suite: pathlib.Path) -> tuple[pathlib.Path, int, str]:
    run = subprocess.run([clang_tidy, "-p", build_dir, "--quiet", str(suite)],
                         capture_output=True, text=True, cwd=source_dir)
    return suite, run.returncode, run.stdout + run.stderr


headers = {}
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
            if m.group(1).startswith(header_root):
                where = m.group(1)[len(str(source_dir)) + 1:]
                for check in checks:
                    headers[(where, int(m.group(2)), int(m.group(3)), check)] = m.group(4)

for (where, line, column, check), message in sorted(headers.items()):
    print(f"{where}:{line}:{column}: {message} [{check}]")
for problem in unparsed:
    print(f"does not parse: {problem}")

print(f"\n{len(headers)} finding(s) in include/cloudevents across {len(suites)} suites"
      + (f"; {len(unparsed)} parse failure(s)" if unparsed else ""))
sys.exit(1 if headers or unparsed else 0)
