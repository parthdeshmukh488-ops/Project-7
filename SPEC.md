# Build specification

Self-contained. Everything needed to build this from nothing, in order, with a
test for "is this milestone actually done".

---

## 0. The thesis, restated precisely

Published latency figures for open-source matching engines are typically
**unfalsifiable**: the instrument's cost is not stated, the clock's resolution
is not stated, the machine's state (pinning, governor, isolation, virtualisation)
is not stated, and the statistic reported is often a mean of a heavy-tailed
distribution.

This project produces (a) a competent matching engine and (b) a measurement
harness that makes its own numbers checkable — and then applies that harness to
other engines on the same hardware.

**The harness is the contribution. The engine is the thing being measured.**

A consequence to accept up front: the honest answer to "how fast is it" may
sometimes be *"below what this machine can resolve"*. The harness must be able
to say that and refuse to publish, rather than printing a number.

---

## 1. Language and toolchain

**C++20**, GCC 13+ / Clang 18+, CMake 3.28+.

Rationale: this is a systems project and C++ is what the domain uses, so the
language is part of the demonstration. Rust would be defensible; mixing both is
not — pick one and be fluent in it.

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Dependencies kept to: **Catch2** (tests), **nanobench** or hand-rolled timing
(see §4 — prefer hand-rolled, the point is to control the instrument).
No Boost. No conan/vcpkg; use FetchContent.

---

## 2. Milestones

Each milestone has a **done test**. Do not start the next one until it passes.

### M0 — Environment characterisation *(do this first, before any engine code)*

This is deliberately milestone zero. Writing it first forces the measurement
question to be answered before there is a number anyone is attached to.

Build `src/measure/`:

- `tsc.hpp` — read the timestamp counter with proper serialisation
  (`__rdtscp` + `lfence`; know why `rdtsc` alone is wrong), calibrate against
  `CLOCK_MONOTONIC_RAW`, verify `constant_tsc` and `nonstop_tsc` in
  `/proc/cpuinfo` and **fail loudly if absent**.
- `clock_overhead.cpp` — measure the cost distribution of each timing method:
  `rdtscp`, `clock_gettime(CLOCK_MONOTONIC)`, `std::chrono::steady_clock::now()`.
  Report p50/p99/p99.9 of *the instrument itself*.
- `resolution.cpp` — measure the smallest non-zero delta each clock can report.
- `jitter.cpp` — an empty measured loop, run for 10^7 iterations. This is the
  **noise floor**. Any latency result within it is not a result.
- `environment.cpp` — capture and report: CPU model, governor, `isolcpus`,
  `nohz_full`, IRQ affinity of the measured core, hyperthread sibling state,
  THP setting, whether running under a hypervisor (`hypervisor` CPU flag),
  `perf_event_paranoid`.

**Done test:** `./build/env_report` prints a one-page report ending in a
verdict of `SUITABLE` / `DEGRADED` / `UNSUITABLE` for nanosecond measurement,
with the specific reasons. On a shared cloud VM it must print `UNSUITABLE` and
name the reasons (virtualised TSC, no core isolation, unknown neighbours).

> **Known constraint:** the development container for this project is a
> 4-core shared Xeon VM with no `perf` access and `perf_event_paranoid=2`.
> It is fine for correctness work and useless for the headline numbers.
> Final measurements need a bare-metal Linux box with `isolcpus`, the
> performance governor and a pinned, IRQ-free core. Build everything in the
> container; measure on metal.

### M1 — ITCH 5.0 parser

Nasdaq TotalView-ITCH 5.0, binary, big-endian, variable-length messages.
Spec: `nasdaqtrader.com` → Technical Support → Specifications → Data Products →
`NQTVITCHspecification.pdf`. Free sample files: Nasdaq's public sample
directory (see the ITCH FAQ); academics can request full days under NDA;
LOBSTER is an alternative academic source.

- Zero-copy: parse in place out of an `mmap`ed file, no allocation per message.
- Handle every message type, not just add/cancel/execute — the ones people skip
  (`stock directory`, `trading action`, `cross trade`, `broken trade`) are
  exactly what breaks a book mid-day.
- Big-endian decode without UB: `memcpy` + `std::byteswap`, not reinterpret casts.

**Done test:** parse a full sample day; message-type histogram matches an
independent Python reference parser exactly; total bytes consumed equals file
size with zero trailing bytes.

### M2 — Order book, three ways

Price-time priority. The API is `add`, `cancel`, `replace`, `execute`,
`best_bid`, `best_ask`, `depth(n)`.

Implement **three independent** backends:
1. `std::map<Price, Level>` — the obvious one, the baseline.
2. Intrusive doubly-linked order lists + a flat array of price levels indexed
   by ticks from a moving reference price.
3. A radix/bucket structure over the price range, with a bitmap for occupancy
   so best-bid/ask is a `countl_zero`.

O(1) cancel in all three: orders are reached by ID through a hash map holding
an iterator/pointer directly into the level's list.

**Done test — this is the important one:** replay a full ITCH day through all
three; after **every single message** their complete book state (all levels,
all orders, all quantities) must be byte-identical. Any divergence is a bug in
at least one of them, and finding which is the point of having three.

### M3 — Matching engine

Order types: limit, market, IOC, FOK. Partial fills. Self-trade prevention.

Invariants, asserted continuously in debug builds and spot-checked in release:
- the book never crosses (`best_bid < best_ask`),
- share conservation: shares in = shares resting + shares executed,
- price-time priority is never violated — for any execution, no older order at
  an equal-or-better price remained unfilled.

**Done test:** a property-based suite (random order streams) that checks all
three invariants after every operation, plus the ITCH replay from M2 with
executions compared against the exchange's own reported trades.

### M4 — The measurement, done properly

For each stage — parse, book update, match, publish — and end to end:

- Pin to an isolated core; warm the caches; run the stream from memory.
- Time with `rdtscp`, **subtract the measured instrument overhead from M0**,
  convert via the M0 TSC calibration.
- Report **p50, p90, p99, p99.9, p99.99, max** — never a mean.
- Attach **confidence intervals** to each percentile. Latency is heavy-tailed;
  a p99.9 from 10^5 samples is an estimate with wide error bars. Use order
  statistics for the CI, state the sample count, and collect enough samples
  that the p99.99 CI is tight enough to quote (this will be ≥10^7).
- Report the noise floor from M0 alongside, and mark any percentile inside it.

**Done test:** `./build/bench --report` emits a table where every number has a
CI, the instrument overhead and noise floor are stated, and the environment
verdict from M0 is printed at the top. Re-running on the same machine
reproduces every percentile within its stated CI.

### M5 — The comparison *(what makes it a paper rather than a repo)*

Clone 3–4 of the widely-starred engines. Build them. Run **their own benchmarks**
on **this** hardware, then run them through **this** harness.

Candidates (all C++, all report figures in their README):
- `zachary-olsen/limit-order-book-matching-engine` — validated on real ITCH,
  reports full percentiles; the most rigorous of the set and the fairest
  comparison.
- `ShaneWoloszyn/cpp-matching-engine` — 2.43M ops/sec, p50 300 ns.
- `Konkyana-Samith/Limit-Order-Booking` — ~130 ns average.
- `farhanarshad00/Limit-Order-Book` — p50 125 ns; author already notes the
  timer overhead issue.

Produce a table: published figure, figure reproduced on this hardware, figure
after instrument correction, and the environment each was originally measured
under. **Be scrupulously fair** — the goal is "these numbers are not
comparable and here is why", not "mine is fastest". If one of them beats this
engine, that goes in the README in bold.

**Done test:** the table exists, every row is reproducible from a script, and
the methodology section would let a sceptic rerun it.

### M6 — Strategy and tick-to-trade *(stretch)*

A simple market maker on the replayed book: quote both sides, skew on
inventory, cancel on adverse selection. Measure **tick-to-trade**: ITCH byte
arrival → order bytes out. This is the number a trading firm actually cares
about and nobody in the open-source set measures.

---

## 3. Repository layout

```
include/lob/          public headers
src/
  itch/               feed parser              (M1)
  book/               three book backends      (M2)
  match/              matching engine          (M3)
  measure/            the harness              (M0)
  strategy/           market maker             (M6)
tests/                Catch2 unit + property   (all)
bench/                benchmark drivers        (M4)
compare/              third-party harness      (M5)
  vendor/             git submodules of the engines being compared
docs/
  METHODOLOGY.md      how the measurement works and why
  RESULTS.md          the table, regenerated by script
data/
  sample/             small ITCH slice committed for CI
  .gitignore          full days are 15-25 GB — never commit
```

---

## 4. Measurement methodology — the core of it

Written out because this is the part that must not be got wrong.

**Why `rdtsc` alone is wrong.** It is not a serialising instruction; the CPU
reorders it relative to the code being timed. Use `__rdtscp` (partially
serialising, and returns the core ID so you can detect migration) with an
`lfence` before. Verify `constant_tsc`/`nonstop_tsc` so the counter does not
change rate with frequency or stop in idle states.

**Overhead subtraction.** Measure `t1 = rdtscp(); t2 = rdtscp();` ten million
times. The distribution of `t2 - t1` is the instrument. Subtract its **median**
from each measurement, and never report a result whose median is less than the
instrument's p99 — at that point you are reporting the instrument.

**Why not a mean.** Latency distributions are heavy-tailed and often
multi-modal (cache hit / cache miss / page fault / preemption). A mean is a
number that describes none of those modes and is dominated by the tail. Report
percentiles, plot the full distribution, and state the sample count.

**Confidence intervals on percentiles.** For the *p*-th percentile from *n*
samples, the CI comes from the binomial order statistics: the *k*-th smallest
sample where `k ~ Binomial(n, p)`. The width grows sharply as `p → 1`, which is
precisely why p99.99 needs ≥10^7 samples to be worth quoting. **Compute this;
do not quote a percentile without it.**

**Coordinated omission.** If the harness measures "time to process the order I
just submitted" and only submits the next order once the last finished, it
never observes queueing and systematically under-reports under load. Either
drive at a fixed open-loop rate and measure against *intended* send time, or
state explicitly that the figure is a service time, not a response time. Most
of the surveyed projects do the latter without saying so.

---

## 5. What "done" looks like

A README whose headline is a comparison table rather than a single number,
backed by a methodology document a sceptic could follow to disagree, and a
harness that refuses to print numbers the machine cannot support.

If the engine turns out to be mid-table on speed, the project still works —
because the contribution is the measurement, and a credible measurement that
embarrasses its author is more convincing than a flattering one that cannot be
checked.
