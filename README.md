# A limit order book whose latency numbers survive scrutiny

A Nasdaq ITCH 5.0 feed handler, limit order book and matching engine — and,
the actual point of the project, a measurement harness that can tell the
difference between a fast matching engine and a fast clock.

## Why this, when there are forty of these on GitHub

There are. I read them before starting. They are mostly good, and almost all of
them report latency numbers that cannot be checked:

| what they report | what is missing |
|---|---|
| "p50 125 ns, p99 3.4 µs" | clock resolution never stated |
| "~130 ns average per order" | an average, on a heavy-tailed distribution |
| "2.43M ops/sec, p50 300 ns" | no core pinning, governor or isolation stated |
| "p50 300 ns, p99 5.2 ms" | measured under WSL2 |

One of those authors notes in passing that `std::chrono::steady_clock` costs
**20–40 ns per call** on their machine. Another notes their M3's
`steady_clock` ticks every **~42 ns**. Both then report per-order latencies
in the 125–300 ns range — which means somewhere between a sixth and a third
of the published number is the instrument, and the quantisation is a
meaningful fraction of the signal.

None of this makes those projects bad. It makes their headline numbers
**unfalsifiable**, which is a different problem and the one worth solving.

## The claim this project is built to support

> On identical hardware, with the timer overhead measured and subtracted and
> the environment characterised, here is what this engine costs per order —
> and here is what several widely-starred engines cost when measured the same
> way, which is not what their READMEs say.

That is a claim someone can disagree with and check. "125 ns" is not.

## What makes it a hard project rather than a big one

- **Measuring something 100 ns long with a 20 ns instrument.** Requires TSC
  calibration, serialising instructions, overhead subtraction, and knowing
  when the answer is below the noise floor and must not be reported.
- **Real exchange data, not a synthetic stream.** Nasdaq ITCH 5.0 binary, with
  the message mix, the cancel-heavy load and the pathological days that
  synthetic benchmarks never produce.
- **Correctness that is actually checked.** Several independent book
  implementations that must agree bit-for-bit on a full trading day, because
  an order book that is fast and wrong is worthless and easy to write.
- **Heavy-tailed statistics.** Latency distributions have no useful mean.
  Percentiles need confidence intervals, and the interesting ones (p99.99)
  need more samples than people usually collect.

## Status

Specification complete, implementation not started. See **[SPEC.md](SPEC.md)**
for the full build plan, milestone acceptance criteria and measurement
methodology, and **[CLAUDE.md](CLAUDE.md)** if you are an agent picking this up.

## License

MIT
