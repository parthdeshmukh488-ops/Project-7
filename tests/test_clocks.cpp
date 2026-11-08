// The instrument, checked before it is trusted to measure anything.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "lob/measure/clocks.hpp"
#include "lob/measure/environment.hpp"

using namespace lob::measure;
using Catch::Approx;

TEST_CASE("the timestamp counter advances monotonically") {
    auto previous = read_tsc();
    for (int i = 0; i < 1000; ++i) {
        const auto now = read_tsc();
        REQUIRE(now.ticks >= previous.ticks);
        previous = now;
    }
}

TEST_CASE("calibration agrees with a known wall-clock interval") {
    const auto calibration = calibrate_tsc();
    REQUIRE(calibration.ticks_per_ns > 0.1);   // no plausible CPU is below 100 MHz
    REQUIRE(calibration.ticks_per_ns < 20.0);  // nor above 20 GHz

    // Sleep a known duration and check the converted tick count matches.  The
    // tolerance is loose because nanosleep overshoots; the point is to catch a
    // calibration that is wrong by a factor, not by a percent.
    const auto before = read_tsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto after = read_tsc();

    const double measured_ms = calibration.to_ns(after.ticks - before.ticks) / 1e6;
    REQUIRE(measured_ms > 15.0);
    REQUIRE(measured_ms < 40.0);
}

TEST_CASE("calibration is reproducible") {
    const auto a = calibrate_tsc();
    const auto b = calibrate_tsc();
    REQUIRE(a.ticks_per_ns == Approx(b.ticks_per_ns).epsilon(0.01));
}

TEST_CASE("every clock reports a non-negative cost and a resolution") {
    const auto calibration = calibrate_tsc();
    for (auto source : {ClockSource::Rdtscp, ClockSource::ClockGettimeMonotonic,
                        ClockSource::ClockGettimeRaw, ClockSource::ChronoSteady}) {
        const auto c = characterise(source, calibration, 20'000);
        INFO("clock: " << name_of(source));
        REQUIRE(c.overhead_median_ns >= 0.0);
        REQUIRE(c.overhead_p99_ns >= c.overhead_median_ns);
        REQUIRE(c.resolution_ns >= 0.0);
        // A timing call that appears to cost a microsecond means something is
        // badly wrong -- a trap into the kernel on every read, most likely.
        REQUIRE(c.overhead_median_ns < 1000.0);
    }
}

TEST_CASE("rdtscp is not slower than the syscall-based clocks") {
    // The reason the project uses it.  If this ever fails, the platform is
    // doing something unusual and the choice should be revisited rather than
    // assumed.
    const auto calibration = calibrate_tsc();
    const auto tsc = characterise(ClockSource::Rdtscp, calibration, 50'000);
    const auto gettime =
        characterise(ClockSource::ClockGettimeMonotonic, calibration, 50'000);
    REQUIRE(tsc.overhead_median_ns <= gettime.overhead_median_ns * 1.5);
}

TEST_CASE("the noise floor is positive and ordered") {
    const auto calibration = calibrate_tsc();
    const auto floor = measure_noise_floor(calibration, 50'000);
    REQUIRE(floor.median_ns > 0.0);
    REQUIRE(floor.p99_ns >= floor.median_ns);
    REQUIRE(floor.p999_ns >= floor.p99_ns);
    REQUIRE(floor.max_ns >= floor.p999_ns);
}

TEST_CASE("the environment is described and judged") {
    const auto env = inspect_environment();
    REQUIRE(env.logical_cores > 0);
    REQUIRE_FALSE(env.cpu_model.empty());

    // The verdict must follow from the concerns rather than being independent
    // of them: a machine with no concerns cannot be anything but suitable.
    if (env.concerns.empty()) REQUIRE(env.verdict() == Suitability::Suitable);
    if (env.verdict() == Suitability::Suitable) REQUIRE(env.concerns.empty());
}

TEST_CASE("a virtualised machine is never judged suitable") {
    // The specific failure this project exists to make visible: a cloud VM
    // produces confident-looking nanosecond numbers that mean nothing.
    const auto env = inspect_environment();
    if (env.under_hypervisor) {
        REQUIRE(env.verdict() == Suitability::Unsuitable);
        REQUIRE_FALSE(env.concerns.empty());
    }
}
