#!/usr/bin/env python3
"""Turn every C++ block in docs/GUIDE.md into one translation unit.

A block fenced as ```cpp is namespace-scope code, and every such block shares
one namespace, so a later block can use what an earlier one declared. A block
fenced as ```cpp body is a run of statements; it becomes a function that
main() calls, so the example runs as well as compiles. A block fenced as
```cpp nocompile is left out, which is how the guide shows a line that must
not build.

Every block sees the codec alias and the literals, and carries a #line
directive, so a compiler error names the line in the guide.

Usage: extract_guide_snippets.py GUIDE.md OUTPUT.cpp
"""

import pathlib
import re
import sys

FENCE = re.compile(r"^```(\S*)\s*(.*)$")

PRELUDE = """\
// Generated from docs/GUIDE.md by cmake/extract_guide_snippets.py.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/binding/nats.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
#include <cloudevents/message.hpp>

namespace guide {
using codec = ce::codec::nlohmann_codec;
using namespace ce::literals;
}  // namespace guide
"""


def blocks(text):
    """Yield (language, words, first_code_line, lines) for each fenced block."""
    inside = False
    for number, line in enumerate(text.splitlines(), 1):
        match = FENCE.match(line)
        if not inside:
            if match:
                inside = True
                language, words, start, lines = match.group(1), match.group(2).split(), number + 1, []
        elif line.strip() == "```":
            inside = False
            yield language, words, start, lines
        else:
            lines.append(line)
    if inside:
        sys.exit(f"an unterminated code block starts before line {start}")


def main():
    guide, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    source = guide.as_posix()
    units, runs = [], []

    for language, words, start, lines in blocks(guide.read_text(encoding="utf-8")):
        if language != "cpp" or "nocompile" in words:
            continue
        includes = [line for line in lines if line.startswith("#include")]
        code = [line for line in lines if not line.startswith("#include")]
        body = "body" in words
        unit = [*includes, "namespace guide {"]
        if body:
            unit.append(f"void run_line_{start}() {{")
            runs.append(f"  guide::run_line_{start}();")
        unit.append(f'#line {start} "{source}"')
        unit.extend(code)
        if body:
            unit.append("}")
        unit.append("}  // namespace guide")
        units.append("\n".join(unit))

    if not units:
        sys.exit(f"{source} holds no C++ block, so there is nothing to check")

    main_function = "\n".join(["int main() {", *runs, "  return 0;", "}"])
    text = "\n\n".join([PRELUDE, *units, main_function]) + "\n"
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
