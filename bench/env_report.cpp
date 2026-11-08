// The report that gates every later number in this project.
//
// Run first, on whatever machine is about to produce a benchmark.  If it ends
// in UNSUITABLE then any nanosecond figure from that machine is a description
// of the machine's noise, not of the code, and the benchmark driver will
// refuse to publish one.

#include <cstdio>
#include <span>
#include <vector>

#include "lob/measure/clocks.hpp"
#include "lob/measure/environment.hpp"
#include "lob/measure/stats.hpp"

using namespace lob::measure;

namespace {

void rule() { std::printf("  %s\n", std::string(68, '-').c_str()); }

void heading(const char* text) { std::printf("\n%s\n", text); rule(); }

}  // namespace

int main(int argc, char** argv) {
    const std::size_t samples = (argc > 1) ? std::stoul(argv[1]) : 1'000'000;

    std::printf("Measurement environment report\n");
    std::printf("==============================\n");

    // ---- the machine -------------------------------------------------
    const auto env = inspect_environment();
    heading("Machine");
    std::printf("  %-22s %s\n", "cpu", env.cpu_model.c_str());
    std::printf("  %-22s %d%s\n", "logical cores", env.logical_cores,
                env.hyperthreading ? " (hyperthreading on)" : "");
    std::printf("  %-22s %s\n", "hypervisor", env.under_hypervisor ? "yes" : "no");
    std::printf("  %-22s constant_tsc=%s nonstop_tsc=%s\n", "tsc flags",
                env.constant_tsc ? "yes" : "no", env.nonstop_tsc ? "yes" : "no");
    std::printf("  %-22s %s\n", "governor",
                env.governor.empty() ? "(unreadable)" : env.governor.c_str());
    std::printf("  %-22s %s\n", "isolated cores",
                env.isolated_cores.empty() ? "(none)" : env.isolated_cores.c_str());
    std::printf("  %-22s %s\n", "nohz_full",
                env.nohz_full.empty() ? "(none)" : env.nohz_full.c_str());
    std::printf("  %-22s %d\n", "perf_event_paranoid", env.perf_event_paranoid);
    std::printf("  %-22s %.2f\n", "load average (1m)", env.load_average_1m);

    // ---- the counter -------------------------------------------------
    const bool pinned = pin_to_core(0);
    const auto calibration = calibrate_tsc();

    heading("Timestamp counter");
    std::printf("  %-22s %s\n", "pinned to core 0", pinned ? "yes" : "NO (refused)");
    std::printf("  %-22s %.6f (%.3f GHz)\n", "ticks per ns", calibration.ticks_per_ns,
                calibration.ticks_per_ns);
    std::printf("  %-22s %.2e\n", "calibration spread", calibration.residual);
    std::printf("  %-22s %s\n", "usable", calibration.usable ? "yes" : "no");

    // ---- the instruments ---------------------------------------------
    heading("Cost of the instrument itself");
    std::printf("  %-28s %12s %12s %12s\n", "clock", "median ns", "p99 ns", "resolution");
    const ClockSource sources[] = {
        ClockSource::Rdtscp, ClockSource::ClockGettimeMonotonic,
        ClockSource::ClockGettimeRaw, ClockSource::ChronoSteady};
    double rdtscp_overhead = 0.0;
    for (auto source : sources) {
        const auto c = characterise(source, calibration, samples / 4);
        if (source == ClockSource::Rdtscp) rdtscp_overhead = c.overhead_median_ns;
        std::printf("  %-28s %12.2f %12.2f %12.2f\n", name_of(source),
                    c.overhead_median_ns, c.overhead_p99_ns, c.resolution_ns);
    }

    // ---- the floor ---------------------------------------------------
    const auto floor = measure_noise_floor(calibration, samples);
    heading("Noise floor (empty measured interval)");
    std::printf("  %-22s %.2f ns\n", "median", floor.median_ns);
    std::printf("  %-22s %.2f ns\n", "p99", floor.p99_ns);
    std::printf("  %-22s %.2f ns\n", "p99.9", floor.p999_ns);
    std::printf("  %-22s %.2f ns\n", "max", floor.max_ns);
    std::printf("  %-22s %zu\n", "core migrations", floor.core_migrations);
    std::printf("\n  Any measured latency below %.1f ns on this machine is not\n"
                "  distinguishable from nothing happening.\n", floor.p99_ns);

    // ---- what a percentile would be worth here ------------------------
    heading("Samples needed before a percentile is worth quoting");
    std::printf("  %-12s %16s %16s\n", "percentile", "for +-10%", "for +-1%");
    for (double p : {0.5, 0.99, 0.999, 0.9999}) {
        const auto loose = samples_needed(p, 0.10);
        const auto tight = samples_needed(p, 0.01);
        std::printf("  p%-11.4g %16zu %16zu\n", p * 100.0, loose, tight);
    }
    std::printf("\n  Rank-space requirement only: a necessary condition, not a\n"
                "  sufficient one -- the achievable width also depends on how\n"
                "  dense the distribution is near that percentile.\n");

    // ---- verdict ------------------------------------------------------
    const auto verdict = env.verdict();
    heading("Verdict");
    std::printf("  %s\n", name_of(verdict));
    if (!env.concerns.empty()) {
        std::printf("\n");
        for (const auto& c : env.concerns) std::printf("  - %s\n", c.c_str());
    }
    if (verdict == Suitability::Unsuitable) {
        std::printf(
            "\n  This machine cannot support a nanosecond latency claim.  Build and\n"
            "  test here; measure on an isolated bare-metal core.\n");
    }
    std::printf("\n  Instrument overhead to subtract from every result: %.2f ns\n",
                rdtscp_overhead);

    return verdict == Suitability::Unsuitable ? 1 : 0;
}
