// What the machine is, and whether it can support a nanosecond claim.
//
// A latency figure is a statement about a machine as much as about code.  The
// same engine measured on an isolated bare-metal core and on a shared cloud
// VM gives numbers that differ by more than most of the optimisations people
// write about -- which is why a figure quoted without its environment cannot
// be compared with anything.
//
// This produces a verdict, not just a description, and the benchmark driver
// refuses to publish headline numbers when the verdict is UNSUITABLE.
#pragma once

#include <string>
#include <vector>

namespace lob::measure {

enum class Suitability { Suitable, Degraded, Unsuitable };

[[nodiscard]] const char* name_of(Suitability s);

struct Environment {
    std::string cpu_model;
    int logical_cores = 0;
    bool hyperthreading = false;
    bool under_hypervisor = false;
    bool constant_tsc = false;
    bool nonstop_tsc = false;
    std::string governor;         ///< empty if unreadable
    std::string isolated_cores;   ///< contents of /sys/devices/system/cpu/isolated
    std::string nohz_full;
    bool transparent_hugepages = false;
    int perf_event_paranoid = -1; ///< -1 if unreadable
    double load_average_1m = 0.0;

    /// Why the machine is not suitable, in the order that matters.
    std::vector<std::string> concerns;

    [[nodiscard]] Suitability verdict() const;
};

[[nodiscard]] Environment inspect_environment();

}  // namespace lob::measure
