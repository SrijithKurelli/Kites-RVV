#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kites::Performance
{

enum class TimingOpKind
{
    ScalarALU,
    VectorALU,
    VectorReduction,
    VectorLoad,
    VectorStore
};

struct TimingInstruction
{
    std::string mnemonic;
    TimingOpKind kind{TimingOpKind::ScalarALU};
    std::string destination;
    std::vector<std::string> sources;
    std::size_t active_elements{0};
    std::uint64_t extra_memory_delay_cycles{0};
};

struct TimingConfig
{
    std::size_t lanes{2};
    std::uint64_t issue_interval_cycles{1};
    std::uint64_t scalar_latency_cycles{3};
    std::uint64_t vector_startup_latency_cycles{2};
    std::uint64_t vector_group_latency_cycles{1};
    std::uint64_t reduction_extra_latency_cycles{1};
    std::uint64_t memory_latency_cycles{4};
};

struct TimingTraceEntry
{
    std::size_t instruction_index{0};
    std::string mnemonic;
    TimingOpKind kind{TimingOpKind::ScalarALU};
    std::uint64_t issue_cycle{0};
    std::uint64_t start_cycle{0};
    std::uint64_t completion_cycle{0};
    std::size_t active_elements{0};
    std::size_t lane_groups{0};
    std::size_t lane_slots{0};
    std::uint64_t dependency_stall_cycles{0};
    std::uint64_t memory_stall_cycles{0};
};

struct TimingSummary
{
    std::size_t instruction_count{0};
    std::size_t vector_instruction_count{0};
    std::size_t active_vector_elements{0};
    std::size_t lane_slots{0};
    double lane_utilization{0.0};
    std::uint64_t estimated_cycles{0};
    std::uint64_t dependency_stall_cycles{0};
    std::uint64_t memory_stall_cycles{0};
    double active_elements_per_cycle{0.0};
    std::vector<TimingTraceEntry> trace;
};

class VectorTimingModel
{
public:
    explicit VectorTimingModel(TimingConfig config = {});
    const TimingConfig &config() const noexcept;
    TimingSummary simulate(const std::vector<TimingInstruction> &instructions) const;

private:
    TimingConfig config_;
};

const char *toString(TimingOpKind kind) noexcept;

} // namespace Kites::Performance
