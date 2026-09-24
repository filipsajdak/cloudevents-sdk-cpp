# The performance job

Every pull request is measured against main and against committed budgets, and fails when an event operation gets costlier.
This page says what is measured, how to read the result, and what to do when a cost is deliberate.
The design is ADR-0011; the rules are `SWR-PERF-0001` to `SWR-PERF-0007`.

## What is measured

Nine operations, each with the four bench codecs (nlohmann, RapidJSON, Boost.JSON and Glaze):
decode of a minimal, a full and a large event, encode of the full event, a round trip, a batch of 100 each way, and a typed payload read and write.
They are the operations `bench/codec_bench.cpp` times, over the documents in `bench/documents.hpp`.

| measure | how | on the pull request |
|---|---|---|
| `instructions` | Callgrind, collecting only inside the measured loop, divided by its iterations, in `bench/perf/perf_probe_instr`: the same operations with no allocation accounting linked in | fails above +2% over main (`SWR-PERF-0001`) |
| `allocations`, `allocated_bytes` | a counting global `operator new` in `bench/perf/perf_probe`, one operation after warm-up | fails on any increase (`SWR-PERF-0002`) |
| `retained_bytes` | live heap bytes while a decoded event is held, minus before the decode | fails above +1% (`SWR-PERF-0003`) |
| any of the above | against `bench/budgets.json` | fails above the budget (`SWR-PERF-0004`) |
| `binary_bytes` | a stripped minimal consumer per codec, `bench/perf/consumer.cpp` | warns above +5% (`SWR-PERF-0006`) |
| `wall_ns`, `cpu_ns` | Google Benchmark, from `codec_bench` | reported only |

The gated measures are deterministic.
Two runs of the same tree give identical instruction counts, and the probe refuses to report an allocation count that differs between three identical runs.
Wall time on a shared runner moves by 10 to 20 percent, which is why it never gates.

On Linux the probe also counts direct `malloc` calls from the code it compiles, which is how RapidJSON's `CrtAllocator` allocates.
Elsewhere it counts `operator new` only, and its output says so in `counts_malloc`.
Every count is of the bytes a caller asked for, never the size the allocator rounded a block up to: that size depends on where in the heap the block landed, so it differs between identical runs.
`test/malloc_accounting_test.cpp` checks the `malloc` counting on Linux, `realloc` in every form included.

## Reading the pull request table

The job writes one table to its summary and to a single comment on the pull request, updated on each push.
Failures come first, each with the requirement it breaks, then any warnings, then the table.
Each row is one id and measure: main, the pull request, the change, the budget and the verdict.
The measurements within their limits are folded away below.
When the pull request cannot be measured at all, the comment has no table: it names the id and mode that failed and quotes what the probe said, and the job fails.

An id is `<operation>/<codec>`, such as `decode_full/rapidjson`; `consumer/<codec>` carries the binary size.

Main is the merge base, built on the same runner and measured with the pull request's own probe, so both sides run the same harness.
When main does not build with that probe, it is measured with its own, and the table says so.
When main cannot be measured at all, only the budgets gate, and the table says that too.

A pull request from a fork gets the table in the job summary only: its token cannot comment.

## When a cost is deliberate

Raise the budget in `bench/budgets.json` in the same pull request, and say in the commit why the operation needs it.
The reviewer then sees the cost as a one-line diff next to the change that needs it.

A raised budget accepts the growth over main in that id and measure.
The job compares the pull request's budget with main's, and a growth whose budget went up is listed under "Costs accepted by a raised budget" instead of failing.
The new budget still gates, so raise it to what the change needs plus some headroom, not further.

A new operation needs budgets before it can merge.
The failure message proposes one: the measurement plus 10% headroom.

## Reproducing locally

The job's toolchain is `bench/perf/install_toolchain.sh` on Ubuntu 24.04, and `bench/perf/Dockerfile` runs the same script.

```bash
bench/perf/run_in_docker.sh . build/perf-results/results.json
python3 bench/perf/compare.py --base build/perf-results/main.json \
  --head build/perf-results/results.json --budgets bench/budgets.json
```

Keep the paths under your home directory: Colima and Docker Desktop share only that with the container.

CI runs on x86-64, and an instruction count depends on the architecture.
On an Apple Silicon Mac the container is arm64, so its counts compare only with another arm64 run, and `compare.py` refuses to apply the budgets to them.
`CE_PERF_PLATFORM=linux/amd64 bench/perf/run_in_docker.sh ...` runs the x86-64 image under emulation; it is slow, and it gives CI's counts to within the differences between CPUs.

Without Docker, a quick look at allocations and retained bytes needs only the bench build:

```bash
cmake -S . -B build/perf -DCMAKE_BUILD_TYPE=Release -DCE_BUILD_BENCHMARKS=ON
cmake --build build/perf --target perf_measure
python3 bench/perf/measure.py --build-dir build/perf --out results.json --no-instructions
```

## Re-seeding the budgets

Budgets are seeded from the CI toolchain, never from a developer machine.

1. Run the `perf` workflow by hand, from the Actions tab, with `seed` ticked.
2. Download its `proposed-budgets` artifact.
3. Commit its `budgets.json` over `bench/budgets.json`, with the reason in the commit.

Re-seed when the CI image changes compiler, when `compare.py` reports that the budgets were seeded on another toolchain, or when many budgets have drifted far above main.
The file records the toolchain and the commit it was seeded from.

## The history of main

Each merge to main appends a record to `history.jsonl` on the orphan `bench-data` branch, and regenerates the `README.md` there.
It shows the last 30 runs of main for each operation, with a trend marker per measure.
Merges in quick succession share one concurrency group, and GitHub keeps only the newest pending run in it, so a burst of merges records the last of them.

## Limits

- Instruction count is a proxy for time.
  It does not see cache misses or branch mispredictions, so wall time on quiet hardware remains the measure of record for a release; `bench/README.md` has that comparison.
- glibc picks `memcpy` and friends by CPU features, so two x86-64 runners can differ slightly in absolute counts.
  Main and the pull request share a runner, so the comparison is unaffected; the budgets' 10% headroom absorbs the rest.
