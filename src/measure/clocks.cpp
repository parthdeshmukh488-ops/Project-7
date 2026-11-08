#include "lob/measure/clocks.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <string>

#include <pthread.h>
#include <sched.h>

#include "lob/measure/stats.hpp"

namespace lob::measure {
namespace {

std::uint64_t nanos_from(clockid_t clock) {
    timespec ts{};
    clock_gettime(clock, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

bool cpuinfo_has_flag(const std::string& flag) {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("flags", 0) == 0) {
            return line.find(" " + flag + " ") != std::string::npos ||
                   line.find(" " + flag) == line.size() - flag.size() - 1;
        }
    }
    return false;
}

double percentile_of(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    auto idx = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())));
    return v[std::min(idx == 0 ? 0 : idx - 1, v.size() - 1)];
}

}  // namespace

const char* name_of(ClockSource source) {
    switch (source) {
        case ClockSource::Rdtscp: return "rdtscp";
        case ClockSource::ClockGettimeMonotonic: return "clock_gettime(MONOTONIC)";
        case ClockSource::ClockGettimeRaw: return "clock_gettime(MONOTONIC_RAW)";
        case ClockSource::ChronoSteady: return "chrono::steady_clock";
    }
    return "unknown";
}

bool pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

TscCalibration calibrate_tsc(int repeats, int window_ms) {
    TscCalibration out{};
    out.constant_tsc = cpuinfo_has_flag("constant_tsc");
    out.nonstop_tsc = cpuinfo_has_flag("nonstop_tsc");

#if !LOB_HAVE_RDTSC
    out.ticks_per_ns = 1.0;
    out.usable = false;
    return out;
#else
    std::vector<double> rates;
    rates.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = read_tsc();
        const std::uint64_t n0 = nanos_from(CLOCK_MONOTONIC_RAW);

        timespec sleep{0, static_cast<long>(window_ms) * 1'000'000L};
        nanosleep(&sleep, nullptr);

        const auto t1 = read_tsc();
        const std::uint64_t n1 = nanos_from(CLOCK_MONOTONIC_RAW);

        if (n1 > n0 && t1.ticks > t0.ticks) {
            rates.push_back(static_cast<double>(t1.ticks - t0.ticks) /
                            static_cast<double>(n1 - n0));
        }
    }

    if (rates.empty()) {
        out.ticks_per_ns = 1.0;
        out.usable = false;
        return out;
    }

    std::sort(rates.begin(), rates.end());
    out.ticks_per_ns = rates[rates.size() / 2];
    out.residual = (rates.back() - rates.front()) / out.ticks_per_ns;

    // A counter that changes rate with core frequency, or stops in idle, makes
    // every interval conversion wrong by an unknown amount.  Spread across
    // repeats catches a virtualised counter that the cpuinfo flags do not.
    out.usable = out.constant_tsc && out.nonstop_tsc && out.residual < 0.001;
    return out;
#endif
}

ClockCharacter characterise(ClockSource source, const TscCalibration& calibration,
                            std::size_t samples) {
    std::vector<double> costs;
    costs.reserve(samples);

    // Back-to-back readings: the interval between two consecutive reads of the
    // same clock, with nothing in between, is the cost of the reading.
    for (std::size_t i = 0; i < samples; ++i) {
        double ns = 0.0;
        switch (source) {
            case ClockSource::Rdtscp: {
                const auto a = read_tsc();
                const auto b = read_tsc();
                ns = calibration.to_ns(b.ticks - a.ticks);
                break;
            }
            case ClockSource::ClockGettimeMonotonic: {
                const auto a = nanos_from(CLOCK_MONOTONIC);
                const auto b = nanos_from(CLOCK_MONOTONIC);
                ns = static_cast<double>(b - a);
                break;
            }
            case ClockSource::ClockGettimeRaw: {
                const auto a = nanos_from(CLOCK_MONOTONIC_RAW);
                const auto b = nanos_from(CLOCK_MONOTONIC_RAW);
                ns = static_cast<double>(b - a);
                break;
            }
            case ClockSource::ChronoSteady: {
                const auto a = std::chrono::steady_clock::now();
                const auto b = std::chrono::steady_clock::now();
                ns = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
                break;
            }
        }
        costs.push_back(ns);
    }

    // Resolution: the smallest interval the clock can express at all.  For a
    // coarse clock most back-to-back deltas are zero and the rest are
    // multiples of one tick, so the smallest non-zero delta is the tick.
    double resolution = 0.0;
    {
        std::vector<double> nonzero;
        nonzero.reserve(costs.size());
        for (double c : costs)
            if (c > 0.0) nonzero.push_back(c);
        if (!nonzero.empty())
            resolution = *std::min_element(nonzero.begin(), nonzero.end());
    }

    ClockCharacter out{};
    out.source = source;
    out.samples = samples;
    out.resolution_ns = resolution;
    out.overhead_median_ns = percentile_of(costs, 0.5);
    out.overhead_p99_ns = percentile_of(costs, 0.99);
    return out;
}

NoiseFloor measure_noise_floor(const TscCalibration& calibration, std::size_t samples) {
    std::vector<double> gaps;
    gaps.reserve(samples);
    std::size_t migrations = 0;

    for (std::size_t i = 0; i < samples; ++i) {
        const auto a = read_tsc();
        asm volatile("" ::: "memory");  // an empty interval the compiler may not remove
        const auto b = read_tsc();
        if (a.core != b.core) ++migrations;
        gaps.push_back(calibration.to_ns(b.ticks - a.ticks));
    }

    NoiseFloor out{};
    out.samples = samples;
    out.core_migrations = migrations;
    out.median_ns = percentile_of(gaps, 0.5);
    out.p99_ns = percentile_of(gaps, 0.99);
    out.p999_ns = percentile_of(gaps, 0.999);
    out.max_ns = gaps.empty() ? 0.0 : gaps.back();  // sorted by percentile_of
    return out;
}

}  // namespace lob::measure
