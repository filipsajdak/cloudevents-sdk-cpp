# ADR-0011: Performance is gated on deterministic measures, measured against main in the same job

## Status

Accepted 2026-09-24, for `STK-PERF-0001`.

## Context

The owner asked for the performance and resource cost of every pull request to be measured, compared with main and with set standards, and acted on when it gets worse.
Three facts shape how.

**Wall time on a shared runner is noise at the scale that matters.**
GitHub-hosted runners vary by 10 to 20 percent between runs, and the gains this SDK makes are often smaller than that.
A gate on wall time would fail pull requests for nothing, and would soon be ignored.

**Some costs are exact.**
The instructions an operation executes under Valgrind, the allocations it makes and the bytes a decoded event keeps alive do not depend on what else the machine is doing.
They can gate with a tight threshold.

**The library is header-only, and its codec is the consumer's choice.**
Its cost depends on the codec, so each measure is taken per codec, and its code lands in every consumer binary.

## Decision

**What is measured, per operation and per codec.**
The operations are the ones `bench/codec_bench.cpp` already covers: decode of a minimal, a full and a large event, encode, round trip, a batch of 100 each way, and a typed payload read and write.
Later stages of CR-0003 add their own.
The codecs are the four bench codecs: nlohmann, RapidJSON, Boost.JSON and Glaze.

| measure | how | gates |
|---|---|---|
| instructions per operation | Callgrind with collection toggled on around the measured loop only, measured in a probe without allocation accounting | fails above +2% over main (`SWR-PERF-0001`) |
| allocations per operation, count and bytes | a counting global allocator in the measuring binary | fails on any increase (`SWR-PERF-0002`) |
| bytes a decoded event retains | the counting allocator's live bytes while the event is held, per payload size | fails above +1% (`SWR-PERF-0003`) |
| budgets | `bench/budgets.json`, seeded from main plus 10% headroom | fails when exceeded (`SWR-PERF-0004`) |
| wall and CPU time | Google Benchmark, as today | reported only |
| binary size | a stripped minimal consumer per codec | warns above +5% (`SWR-PERF-0006`) |

**Main and the pull request are measured in the same job.**
The job builds the merge base and the head on one runner and measures both, so runner and toolchain differences cancel out.
That comparison gates; the budgets gate too.

**Everything stays in the repository.**
The job writes the table to the workflow summary, and comments it on the pull request where the token permits (`SWR-PERF-0005`).
On every merge to main it appends a JSON record to an orphan `bench-data` branch and regenerates a Markdown trend report there (`SWR-PERF-0007`).
No external service, account or token is involved.

**A deliberate cost is an explicit edit.**
A pull request that needs more instructions, an allocation or more retained memory raises the budget in `bench/budgets.json` in the same change.
The reviewer then sees the cost as a diff line, not as a red job.

## Consequences

### Positive
- A regression is caught in the pull request that makes it, with the number beside it.
- The gates do not fail for runner noise, so a red job means something.
- The budgets catch a slow drift that no single pull request makes.
- The history shows when each measure moved, without an external service.

### Negative
- Callgrind runs each benchmark 20 to 50 times slower, so the job uses fewer iterations and takes several minutes.
- Instruction count is a proxy for time: it does not see cache misses or branch mispredictions.
  Wall time remains the measure of record for a release, taken on quiet hardware.
- Glaze is not packaged on the CI image, so the job builds a pinned release from source.
- A comment from a fork's pull request needs a separate, privileged workflow; until one exists, forks see the table in the job summary only.

### Neutral
- `bench/README.md` stays the place for the one-off codec comparison on quiet hardware; this job answers a different question, "did this change make it worse".

## References

- `STK-PERF-0001`, `SYS-PERF-0001`, `SWR-PERF-0001` to `SWR-PERF-0007`
- `bench/codec_bench.cpp`, `bench/README.md`
- Valgrind Callgrind manual, `--toggle-collect` and client requests
