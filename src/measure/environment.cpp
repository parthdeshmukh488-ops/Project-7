#include "lob/measure/environment.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>

namespace lob::measure {
namespace {

std::string read_first_line(const char* path) {
    std::ifstream in(path);
    std::string line;
    if (in && std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) line.pop_back();
        return line;
    }
    return {};
}

std::string cpuinfo_field(const std::string& key) {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) == 0) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            return value;
        }
    }
    return {};
}

bool cpuinfo_flag(const std::string& flag) {
    const auto flags = " " + cpuinfo_field("flags") + " ";
    return flags.find(" " + flag + " ") != std::string::npos;
}

}  // namespace

const char* name_of(Suitability s) {
    switch (s) {
        case Suitability::Suitable: return "SUITABLE";
        case Suitability::Degraded: return "DEGRADED";
        case Suitability::Unsuitable: return "UNSUITABLE";
    }
    return "UNKNOWN";
}

Suitability Environment::verdict() const {
    // Anything that makes a nanosecond figure meaningless rather than merely
    // noisy is disqualifying on its own.
    const bool fatal =
        under_hypervisor || !constant_tsc || !nonstop_tsc || isolated_cores.empty();
    if (fatal) return Suitability::Unsuitable;
    if (!concerns.empty()) return Suitability::Degraded;
    return Suitability::Suitable;
}

Environment inspect_environment() {
    Environment env;
    env.cpu_model = cpuinfo_field("model name");
    env.logical_cores = static_cast<int>(std::thread::hardware_concurrency());
    env.under_hypervisor = cpuinfo_flag("hypervisor");
    env.constant_tsc = cpuinfo_flag("constant_tsc");
    env.nonstop_tsc = cpuinfo_flag("nonstop_tsc");
    env.governor =
        read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    env.isolated_cores = read_first_line("/sys/devices/system/cpu/isolated");
    env.nohz_full = read_first_line("/sys/devices/system/cpu/nohz_full");

    const auto thp = read_first_line("/sys/kernel/mm/transparent_hugepage/enabled");
    env.transparent_hugepages = thp.find("[always]") != std::string::npos;

    const auto paranoid = read_first_line("/proc/sys/kernel/perf_event_paranoid");
    if (!paranoid.empty()) {
        try {
            env.perf_event_paranoid = std::stoi(paranoid);
        } catch (...) {
            env.perf_event_paranoid = -1;
        }
    }

    {
        std::ifstream in("/proc/loadavg");
        if (in) in >> env.load_average_1m;
    }

    // Siblings differing from cores means SMT is on; a hyperthread sibling
    // competing for the same execution units is a large, invisible source of
    // tail latency.
    const auto siblings = cpuinfo_field("siblings");
    const auto cores = cpuinfo_field("cpu cores");
    env.hyperthreading = !siblings.empty() && !cores.empty() && siblings != cores;

    if (env.under_hypervisor)
        env.concerns.emplace_back(
            "running under a hypervisor: the timestamp counter may be "
            "virtualised and neighbouring tenants are not observable");
    if (!env.constant_tsc)
        env.concerns.emplace_back(
            "no constant_tsc: counter rate follows core frequency, so tick "
            "to nanosecond conversion is not a constant");
    if (!env.nonstop_tsc)
        env.concerns.emplace_back(
            "no nonstop_tsc: the counter halts in deeper idle states");
    if (env.isolated_cores.empty())
        env.concerns.emplace_back(
            "no isolated cores (isolcpus): the scheduler may place other work "
            "on the measured core at any time");
    if (env.nohz_full.empty())
        env.concerns.emplace_back(
            "no nohz_full: the periodic timer tick interrupts the measured core");
    if (!env.governor.empty() && env.governor != "performance")
        env.concerns.emplace_back("cpu governor is '" + env.governor +
                                  "', not 'performance': frequency may change mid-run");
    if (env.hyperthreading)
        env.concerns.emplace_back(
            "hyperthreading enabled: a sibling thread shares execution units "
            "with the measured core");
    if (env.perf_event_paranoid > 1)
        env.concerns.emplace_back(
            "perf_event_paranoid > 1: hardware performance counters are "
            "unavailable, so cache and branch behaviour cannot be attributed");
    if (env.load_average_1m > 0.5 * env.logical_cores)
        env.concerns.emplace_back("machine is already loaded");

    return env;
}

}  // namespace lob::measure
