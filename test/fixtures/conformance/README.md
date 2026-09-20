# Conformance fixtures

Every fenced example from the CloudEvents v1.0.2 core, JSON format and HTTP
binding documents, stored **verbatim**: the published bytes, with no
reformatting, no filled-in placeholders and no repaired whitespace. That is the
point of the fixture (SWR-SEC-0004) - a paraphrased copy is where a divergence
from the specification survives a green suite.

Sources, at tag `v1.0.2`:

- `cloudevents/spec.md` -> `core-*`
- `cloudevents/formats/json-format.md` -> `json-*`
- `cloudevents/bindings/http-protocol-binding.md` -> `http-*`

The numbering is the order the examples appear in each document.

## Some examples are illustrations, not wire bytes

Several carry placeholder text - `Content-Length: nnnn`,
`... application data ...`, `...raw binary bytes...`,
`"... base64 encoded string ..."`. Those documents cannot be decoded as
published, and `conformance_test.cpp` says so rather than quietly asserting
something weaker.

The test pins the classification itself: it scans each fixture for placeholder
markers and asserts the result matches what the table expects. If a future
revision of the specification replaces a placeholder with real bytes, that
assertion fails and the fixture gets a real decode rather than staying on the
weaker path unnoticed.

`json-01` and `json-09` are elided only inside `data_base64`. Everything else in
them is real, so the test asserts the whole document decodes far enough to reach
that member and is then rejected with `invalid_base64` - which is the correct
answer for the published bytes.

## Regenerating

`interop/fetch_conformance.sh` re-downloads the three documents and re-extracts
the blocks. It overwrites the fixtures, so a diff after running it is a change
in the specification.
