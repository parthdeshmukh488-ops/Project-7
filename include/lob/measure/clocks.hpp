// Timing primitives, and the measurement of the primitives themselves.
//
// The central problem of this project in one sentence: the thing being
// measured costs around a hundred nanoseconds and the instrument costs around
// twenty, so the instrument has to be characterised before any result means
// anything.
//
// `rdtsc` alone is wrong for this.  It is not a serialising instruction, so
// the processor is free to execute it out of order with respect to the code
// being timed -- the measured interval can exclude work that happened inside
// it, or include work that did not.  `rdtscp` waits for prior instructions to
// retire and additionally returns the core id, which is how migration between
// cores (and therefore between unsynchronised counters) is detected rather
// than silently averaged in.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lob::measure {

#if defined(__x86_64__) || defined(_M_X64)
#define LOB_HAVE_RDTSC 1
#else
#define LOB_HAVE_RDTSC 0
#endif

/// A timestamp counter reading together with the core that produced it.
struct Stamp {
    std::uint64_t ticks;
    std::uint32_t core;
};

/// Serialising timestamp read.
///
/// `lfence` first so earlier loads have completed; `rdtscp` itself waits for
/// prior instructions to retire.  The pair is the conventional minimum for a
/// reading that brackets the code around it rather than floating through it.
[[nodiscard]] inline Stamp read_tsc() noexcept {
#if LOB_HAVE_RDTSC
    std::uint32_t low, high, aux;
    asm volatile("lfence" ::: "memory");
    asm volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(aux)::"memory");
    return Stamp{(static_cast<std::uint64_t>(high) << 32) | low, aux};
#else
    return Stamp{0, 0};
#endif
}

/// Which timing facility a measurement used.
enum class ClockSource { Rdtscp, ClockGettimeMonotonic, ClockGettimeRaw, ChronoSteady };

[[nodiscard]] const char* name_of(ClockSource source);

/// The cost and granularity of a timing facility, measured rather than assumed.
struct ClockCharacter {
    ClockSource source;
    double overhead_median_ns;  ///< cost of one reading
    double overhead_p99_ns;     ///< the figure a result must clear to mean anything
    double resolution_ns;       ///< smallest non-zero interval it can express
    std::size_t samples;
};

/// Ticks per nanosecond, calibrated against CLOCK_MONOTONIC_RAW.
///
/// RAW rather than CLOCK_MONOTONIC because the latter is slewed by NTP, which
/// over a calibration window is a systematic error in the conversion factor.
struct TscCalibration {
    double ticks_per_ns;
    double residual;       ///< relative spread across repeated calibrations
    bool constant_tsc;     ///< counter rate independent of core frequency
    bool nonstop_tsc;      ///< counter keeps running through idle states
    bool usable;           ///< both flags present and the residual is small

    [[nodiscard]] double to_ns(std::uint64_t ticks) const {
        return static_cast<double>(ticks) / ticks_per_ns;
    }
};

/// Calibrate the timestamp counter.  Takes roughly `window_ms` milliseconds.
[[nodiscard]] TscCalibration calibrate_tsc(int repeats = 5, int window_ms = 50);

/// Measure the cost distribution of a timing facility.
[[nodiscard]] ClockCharacter characterise(ClockSource source,
                                          const TscCalibration& calibration,
                                          std::size_t samples = 1'000'000);

/// The noise floor: an empty measured interval, repeated.
///
/// Whatever this reports is the smallest latency the machine can distinguish
/// from nothing.  A result inside it is not a result, and the report says so
/// rather than printing it.
struct NoiseFloor {
    double median_ns;
    double p99_ns;
    double p999_ns;
    double max_ns;
    std::size_t samples;
    std::size_t core_migrations;  ///< non-zero means the pinning failed
};

[[nodiscard]] NoiseFloor measure_noise_floor(const TscCalibration& calibration,
                                             std::size_t samples = 1'000'000);

/// Pin the calling thread to one core.  False if the platform refused.
bool pin_to_core(int core);

}  // namespace lob::measure
