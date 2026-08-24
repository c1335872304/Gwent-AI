#pragma once

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>

namespace gwent {

struct PerformanceCounters {
    std::uint64_t apply_calls = 0;
    std::uint64_t legal_actions_calls = 0;
    std::uint64_t current_decision_calls = 0;
    std::uint64_t entity_index_builds = 0;
    std::uint64_t target_selector_calls = 0;
    std::uint64_t invariant_checks = 0;
    std::uint64_t snapshot_json_calls = 0;

    std::uint64_t kernel_tasks_processed = 0;
    std::uint64_t target_candidates_scanned = 0;

    std::uint64_t apply_ns = 0;
    std::uint64_t legal_actions_ns = 0;
    std::uint64_t current_decision_ns = 0;
    std::uint64_t entity_index_build_ns = 0;
    std::uint64_t target_selector_ns = 0;
    std::uint64_t invariant_ns = 0;
    std::uint64_t snapshot_json_ns = 0;

    void reset() noexcept { *this = PerformanceCounters{}; }
};

enum class PerformanceMetric : std::uint8_t {
    Apply,
    LegalActions,
    CurrentDecision,
    EntityIndexBuild,
    TargetSelector,
    InvariantCheck,
    SnapshotJson,
};

inline void record_performance_call(PerformanceCounters* counters, PerformanceMetric metric) noexcept {
    if (counters == nullptr) {
        return;
    }
    switch (metric) {
        case PerformanceMetric::Apply:
            ++counters->apply_calls;
            break;
        case PerformanceMetric::LegalActions:
            ++counters->legal_actions_calls;
            break;
        case PerformanceMetric::CurrentDecision:
            ++counters->current_decision_calls;
            break;
        case PerformanceMetric::EntityIndexBuild:
            ++counters->entity_index_builds;
            break;
        case PerformanceMetric::TargetSelector:
            ++counters->target_selector_calls;
            break;
        case PerformanceMetric::InvariantCheck:
            ++counters->invariant_checks;
            break;
        case PerformanceMetric::SnapshotJson:
            ++counters->snapshot_json_calls;
            break;
    }
}

inline void record_performance_duration(
    PerformanceCounters* counters,
    PerformanceMetric metric,
    std::uint64_t nanoseconds
) noexcept {
    if (counters == nullptr) {
        return;
    }
    switch (metric) {
        case PerformanceMetric::Apply:
            counters->apply_ns += nanoseconds;
            break;
        case PerformanceMetric::LegalActions:
            counters->legal_actions_ns += nanoseconds;
            break;
        case PerformanceMetric::CurrentDecision:
            counters->current_decision_ns += nanoseconds;
            break;
        case PerformanceMetric::EntityIndexBuild:
            counters->entity_index_build_ns += nanoseconds;
            break;
        case PerformanceMetric::TargetSelector:
            counters->target_selector_ns += nanoseconds;
            break;
        case PerformanceMetric::InvariantCheck:
            counters->invariant_ns += nanoseconds;
            break;
        case PerformanceMetric::SnapshotJson:
            counters->snapshot_json_ns += nanoseconds;
            break;
    }
}

inline void record_kernel_task_processed(PerformanceCounters* counters) noexcept {
    if (counters != nullptr) {
        ++counters->kernel_tasks_processed;
    }
}

inline void record_target_candidates_scanned(PerformanceCounters* counters, std::uint64_t count) noexcept {
    if (counters != nullptr) {
        counters->target_candidates_scanned += count;
    }
}

class PerformanceScope {
public:
    PerformanceScope(PerformanceCounters* counters, PerformanceMetric metric) noexcept
        : counters_(counters), metric_(metric), start_(Clock::now()) {
        record_performance_call(counters_, metric_);
    }

    PerformanceScope(const PerformanceScope&) = delete;
    PerformanceScope& operator=(const PerformanceScope&) = delete;

    ~PerformanceScope() noexcept {
        if (counters_ == nullptr) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count();
        if (elapsed > 0) {
            record_performance_duration(counters_, metric_, static_cast<std::uint64_t>(elapsed));
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    PerformanceCounters* counters_ = nullptr;
    PerformanceMetric metric_ = PerformanceMetric::Apply;
    Clock::time_point start_{};
};

inline std::string performance_counters_to_json(const PerformanceCounters& counters, bool pretty = true) {
    const char* newline = pretty ? "\n" : "";
    const char* indent = pretty ? "  " : "";
    const char* sep = pretty ? ": " : ":";

    std::ostringstream out;
    out << "{" << newline;
    auto field = [&](std::string name, std::uint64_t value, bool last = false) {
        out << indent << '"' << name << '"' << sep << value;
        if (!last) {
            out << ',';
        }
        out << newline;
    };

    field("apply_calls", counters.apply_calls);
    field("legal_actions_calls", counters.legal_actions_calls);
    field("current_decision_calls", counters.current_decision_calls);
    field("entity_index_builds", counters.entity_index_builds);
    field("target_selector_calls", counters.target_selector_calls);
    field("invariant_checks", counters.invariant_checks);
    field("snapshot_json_calls", counters.snapshot_json_calls);
    field("kernel_tasks_processed", counters.kernel_tasks_processed);
    field("target_candidates_scanned", counters.target_candidates_scanned);
    field("apply_ns", counters.apply_ns);
    field("legal_actions_ns", counters.legal_actions_ns);
    field("current_decision_ns", counters.current_decision_ns);
    field("entity_index_build_ns", counters.entity_index_build_ns);
    field("target_selector_ns", counters.target_selector_ns);
    field("invariant_ns", counters.invariant_ns);
    field("snapshot_json_ns", counters.snapshot_json_ns, true);
    out << "}";
    return out.str();
}

}  // namespace gwent
