#!/usr/bin/env bash
# Re-extract the conformance fixtures from the CloudEvents v1.0.2 documents.
# Committed so a fixture can be regenerated and audited (SWR-SEC-0004).
set -euo pipefail

tag="${1:-v1.0.2}"
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/test/fixtures/conformance"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

base="https://raw.githubusercontent.com/cloudevents/spec/$tag/cloudevents"
curl -fsSL "$base/spec.md" -o "$tmp/spec.md"
curl -fsSL "$base/formats/json-format.md" -o "$tmp/json-format.md"
curl -fsSL "$base/bindings/http-protocol-binding.md" -o "$tmp/http-protocol-binding.md"

python3 - "$tmp" "$out" <<'PY'
import re, sys, pathlib

src = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])

names = {
    ("spec.md", 0): "core-01-example-event.json",
    ("json-format.md", 0): "json-01-structured-event.json",
    ("json-format.md", 1): "json-02-binary-headers.http",
    ("json-format.md", 2): "json-03-structured-event.json",
    ("json-format.md", 3): "json-04-binary-headers.http",
    ("json-format.md", 4): "json-05-structured-event.json",
    ("json-format.md", 5): "json-06-binary-headers.http",
    ("json-format.md", 6): "json-07-structured-event.json",
    ("json-format.md", 7): "json-08-binary-headers.http",
    ("json-format.md", 8): "json-09-batch.json",
    ("json-format.md", 9): "json-10-empty-batch.json",
    ("http-protocol-binding.md", 0): "http-01-binary-request.http",
    ("http-protocol-binding.md", 1): "http-02-binary-response.http",
    ("http-protocol-binding.md", 2): "http-03-structured-content-type.http",
    ("http-protocol-binding.md", 3): "http-04-structured-request.http",
    ("http-protocol-binding.md", 4): "http-05-structured-response.http",
    ("http-protocol-binding.md", 5): "http-06-batch-content-type.http",
    ("http-protocol-binding.md", 6): "http-07-batch-request.http",
    ("http-protocol-binding.md", 7): "http-08-batch-response.http",
}

written = 0
for doc in ["spec.md", "json-format.md", "http-protocol-binding.md"]:
    text = (src / doc).read_text()
    blocks = re.findall(r'```(\w*)\n(.*?)```', text, re.S)
    for i, (_lang, body) in enumerate(blocks):
        name = names.get((doc, i))
        if name is None:
            print(f"WARNING: {doc} block {i} has no fixture name", file=sys.stderr)
            continue
        (out / name).write_bytes(body.encode())
        written += 1
print(f"wrote {written} fixtures to {out}")
PY
