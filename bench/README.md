# Which JSON library to plug in

`ce::json::json_codec` is a concept, so the JSON library is the caller's choice.
This measures that choice on the operations a CloudEvents service actually
performs, and reports what each library costs to compile and carry.

**The short answer: RapidJSON.** Fastest or within noise of fastest on every
CloudEvents-sized workload, the smallest binary, and the quickest to compile.
Boost.JSON matches it on speed and overtakes it on large payloads, at three
times the binary. nlohmann is 2.0 to 2.4 times slower than either and remains
a defensible default for its ubiquity. Glaze needs C++23 and so is unavailable
to anyone on the SDK's C++20 floor.

This page answers "which library". Whether a change made the SDK costlier is
the performance job's question, answered on every pull request from the probe
in `perf/`; see [docs/PERFORMANCE.md](../docs/PERFORMANCE.md).

## Correctness came first, and three of the four needed work

A benchmark of codecs that disagree measures nothing, so `codec_check` runs the
CloudEvents rules a codec is load-bearing for - the Integer/floating
distinction, absent-versus-null, the data-member conflict, byte-for-byte
timestamp round-trip, surrogate pairs - and the benchmark is only meaningful
once it passes. Writing the codecs turned up more than the timings did.

**Glaze's default generic value cannot represent a CloudEvents Integer.**
`glz::generic` stores every number as a `double`. That is correct for
JavaScript and wrong here: the JSON format has an Integer attribute type, and
`SWR-JSON-0024` requires a fractional extension value to be refused. In the
default mode `30` and `30.0` are the same value, so neither rule can be
expressed. The codec uses `glz::generic_json<glz::num_mode::i64>`. A user who
reaches for `glz::generic` because it is the obvious type gets something that
compiles, round-trips, and quietly mistypes every integer extension.

**`glz::generic_json{std::string{...}}` compiles and produces an array.** The
converting constructor resolves a string to the array alternative, so
`make_string` silently produced a one-element array and the encoder emitted
`"specversion":["1.0"]`. It encodes without complaint and fails to decode. The
codec assigns the variant alternative by name instead.

**RapidJSON's allocator model does not fit the concept.** Every mutation wants
an allocator and the concept passes none, so an implementation must reach for a
process-wide one. The codec uses the stateless `CrtAllocator` rather than the
default `MemoryPoolAllocator`, because a pool never returns memory until it is
destroyed: a pooled codec would be faster here and would grow without bound in
a long-running service. That would be a benchmark artefact, not a result.

## Two of the fastest libraries cannot be codecs at all

`find` returns `const value*` - a pointer **into** the document. That is what
lets the format layer tell "absent" from "present and null", which the `data`
rules depend on.

simdjson's on-demand values are a forward-only stream and its DOM elements are
returned by value. yyjson hands out node handles rather than addresses of
stored values. Neither has anything to take the address of, so neither can
satisfy the concept without a shim that defeats the point of using them.

They are measured anyway, parsing the same bytes, because the useful question
is not only "which codec is fastest" but "what is this design costing".

## Results

Apple M-series, Homebrew Clang 23.1.1, `-O2 -DNDEBUG`, 7 repetitions, medians.
Times are per operation.

### Decoding a received event

| | 96 B minimal | 414 B full | 64 KiB payload |
|---|---|---|---|
| **RapidJSON** | **537 ns** | **2690 ns** | 290 us |
| Boost.JSON | 566 ns | 2781 ns | **231 us** |
| Glaze | 612 ns | 3910 ns | 259 us |
| nlohmann | 1257 ns | 6062 ns | 607 us |

### Encoding, round trip, batches of 100

| | encode 414 B | round trip | decode batch | encode batch |
|---|---|---|---|---|
| **RapidJSON** | 3104 ns | **5913 ns** | **103 us** (977k ev/s) | 119 us |
| Boost.JSON | 3252 ns | 6041 ns | 114 us (884k ev/s) | 158 us |
| Glaze | **3122 ns** | 7272 ns | 153 us (657k ev/s) | **109 us** (927k ev/s) |
| nlohmann | 5691 ns | 11585 ns | 236 us (429k ev/s) | 210 us |

### Typed payloads, through the describe seam

| | read | write |
|---|---|---|
| **RapidJSON** | **454 ns** | 438 ns |
| Boost.JSON | 549 ns | 661 ns |
| Glaze | 764 ns | **381 ns** |
| nlohmann | 1170 ns | 832 ns |

### The ceiling neither can reach

| | 414 B | 64 KiB |
|---|---|---|
| simdjson (on-demand) | 136 ns, 3.5 GiB/s | 10.5 us, 4.8 GiB/s |
| yyjson | 324 ns, 1.5 GiB/s | 26.6 us, 1.9 GiB/s |
| fastest codec | 2690 ns | 231 us |

Read this as an upper bound, not a target. simdjson's on-demand parser
materialises only what is asked for, and this asks for one field; the codec path
builds a whole DOM **and** a `ce::event` from it. The gap is the cost of a
general-purpose DOM plus the event model, not waste in any of the four codecs.

### Build cost

Per translation unit that instantiates the format layer, which for a
header-only SDK is every one that touches it. Fastest of three builds.

| | compile | binary | stripped |
|---|---|---|---|
| **RapidJSON** | **2.67 s** | 245 KiB | **182 KiB** |
| nlohmann | 3.48 s | 320 KiB | 216 KiB |
| Glaze | 4.40 s | 439 KiB | 312 KiB |
| Boost.JSON | 5.14 s | 622 KiB | 529 KiB |

Boost.JSON is largest here partly because its source is compiled into the
benchmark rather than linked - see `boost_json_src.cpp` for why.

## Glaze requires C++23

Not a preference: at C++20 it fails to compile on `std::unreachable`,
`std::byteswap`, `std::is_scoped_enum_v` and `static` in a `constexpr` function.
The SDK's floor is C++20, so Glaze is not an option for a consumer on the
documented floor. `bench/CMakeLists.txt` sets C++23 for that reason alone.

## Reading these numbers honestly

**The machine was loaded.** Load average ranged from 15 to 42 across runs
because another build was running on the same host. Absolute times are
therefore inflated.

The ratios are not. Medians for the per-event workloads moved by under 5 percent
across a threefold change in load, which is the check that matters: every codec
is contended equally, so the comparison holds even where the absolutes do not.
The batch figures are the noisiest, moving up to 20 percent, and should be read
as approximate. On a quiet machine expect every absolute number here to be
somewhat lower and the ordering to be the same.

`-O0` is refused rather than reported. The same decode measures 6.4 us at `-O2`
and 112 us at `-O0`, and a table of numbers looks equally authoritative either
way.

## Reproducing

```bash
brew install rapidjson boost glaze simdjson yyjson google-benchmark nlohmann-json
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCE_BUILD_BENCHMARKS=ON \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target codec_check codec_bench
./build/bench/codec_check          # correctness first; it gates the rest
./build/bench/codec_bench --benchmark_repetitions=7 \
  --benchmark_report_aggregates_only=true
./bench/measure_build.sh           # compile time and binary size
```

The documents are in `documents.hpp`, embedded rather than read from
`test/fixtures/`, so a run is reproducible from the binary alone and the sizes
are stated rather than incidental. Every one is a real CloudEvent.
