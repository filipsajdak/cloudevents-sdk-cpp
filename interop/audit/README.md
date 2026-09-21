# Behaviour audit

`main.go` runs a fixed list of cases through the Go CloudEvents SDK and prints
what it observed: the headers it writes for an event, and the event it makes of
a message somebody else wrote. The output is committed as
`test/fixtures/interop/go/behaviour.json`, and `interop_test.cpp` asserts this
SDK's answer to the same cases beside it.

Its reason for existing is a defect it would have caught. The HTTP binding
requires percent-encoding of header values, this SDK and sdk-csharp do it, and
sdk-go and sdk-java do not, so a conformant sender was silently misread for a
release (`cloudevents/spec#1397`, D-HTTP-1). Nothing noticed because cross-SDK
testing compared JSON **documents**, and the defect lived in a binding's header
mapping.

Documents are the easy layer to compare. Everything else needs the other SDK
run, not read, which is what this does.

## Running it

```
docker run --rm -v "$PWD:/w" -w /w/interop/audit -e GOFLAGS=-mod=mod golang:1.22 \
  sh -ec 'go mod tidy >/dev/null 2>&1; go run .' > test/fixtures/interop/go/behaviour.json
```

A diff in that file after an SDK upgrade is the point: it means the other SDK
changed its mind about something, and the suite beside it says whether that
matters.

## What it found, 2026-09-21, against sdk-go v2.15.2

Of 21 cases, 14 agree. The four that differ, and who is right:

| case | Go | this SDK | reading |
|---|---|---|---|
| `time` carrying `+02:00` | rewrites to `Z` | keeps the offset | neither is wrong; the instant survives both, the text does not |
| `ce-time: ...t...z` | rejects | accepts | RFC 3339 section 5.6: parsers SHOULD accept lower case |
| `ce-type: ""` | accepts | rejects | core spec: required attributes MUST be non-empty |
| `ce-subject: ""` | accepts | rejects | core spec: an optional attribute present MUST NOT be empty |

Plus one that is not a divergence: Go accepts `specversion: 0.3` because it
implements 0.3, which this SDK does not claim to.

None of the four corrupts data, which is the difference between this list and
the percent-encoding defect. Three are Go being more lenient or more strict
than its own specification. The first is worth a caller's attention: an event
that round-trips through Go comes back textually different, so comparing
`ce::timestamp` by value reports inequality for the same instant.
