# Working agreement

Read this first if you are an agent picking this project up, in a terminal
session or otherwise. It exists so work can resume cold.

## What this is

A Nasdaq ITCH 5.0 feed handler, limit order book and matching engine in C++20.
The *contribution* is the measurement harness, not the engine — see
[README.md](README.md) for the thesis and [SPEC.md](SPEC.md) for the build
plan. Read both before writing code.

## Where to start

**Milestone 0, in `SPEC.md` §2.** Not M1. The whole project rests on being able
to measure honestly, and writing the harness first means the methodology is
settled before there is a number anyone is attached to.

Current state: specification only. No implementation yet.

## Standards this repo is held to

These match the author's other six repositories; keep them.

- **Every claim is measured against something independent.** Not asserted, not
  benchmarked against itself. For this project that means: the parser against
  an independent reference parser, the three book implementations against each
  other, the engine against third-party engines on the same hardware.
- **Report the limits of a result as prominently as the result.** If a number
  is inside the noise floor, say so instead of printing it. If a percentile has
  wide error bars, print the bars.
- **When a measurement contradicts the expected answer, document it where it
  was found** rather than quietly fixing it. The other repos each carry a
  section of these and they are the most credible parts of the portfolio.
- **Tests are the correctness argument.** Property-based where invariants
  exist; differential where multiple implementations exist. Coverage floor
  enforced in CI, not merely reported.
- **No number in a README that is not reproducible by a script in the repo.**

## Known constraints

- **The cloud container cannot measure nanoseconds.** 4-core shared Xeon VM,
  virtualised TSC, no `perf`, `perf_event_paranoid=2`, no core isolation,
  unknown neighbours. Build and test here; the headline numbers need a
  bare-metal Linux box with `isolcpus`, the performance governor, and a pinned
  IRQ-free core. M0's environment report must print `UNSUITABLE` here — that is
  the harness working, not a bug.
- **ITCH data is large.** A full day is 15–25 GB uncompressed. Never commit it.
  A small slice lives in `data/sample/` for CI; `.gitignore` covers the rest.
  Sources: Nasdaq's public sample directory (see their ITCH FAQ), academic
  access under NDA, or LOBSTER.
- **C++20, one language.** Not Rust, not a mix. The specification explains why.

## What not to do

- Do not start with the order book because it is the fun part. M0 first.
- Do not report a mean latency. Ever. See `SPEC.md` §4.
- Do not tune the engine before the harness can detect whether a change helped.
  Without instrument correction and CIs, "it got 15 ns faster" is noise.
- Do not make the comparison in M5 flattering. If a third-party engine wins,
  that goes in the README in bold. The project's value is that its numbers can
  be trusted, which is destroyed the moment they are curated.

## Commits

Author as the repository owner only. No co-author trailers, no tool
attribution, no model names anywhere in commit messages, code comments or
documentation.

Commit messages: what changed and *why*, in prose. The other six repos set the
register — read one of their logs before writing the first commit here.
