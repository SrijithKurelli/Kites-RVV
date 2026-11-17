#include "profiler/vector_timing_model.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace Kites::Performance
{
namespace
{
using Cycle = std::uint64_t;

Cycle checkedAdd(Cycle lhs, Cycle rhs)
{
    if (rhs > std::numeric_limits<Cycle>::max() - lhs)
        throw std::overflow_error("Vector timing cycle count overflow");
    return lhs + rhs;
}

Cycle checkedMultiply(Cycle lhs, Cycle rhs)
{
    if (lhs != 0 && rhs > std::numeric_limits<Cycle>::max() / lhs)
        throw std::overflow_error("Vector timing latency overflow");
    return lhs * rhs;
}

std::size_t checkedSizeAdd(std::size_t lhs, std::size_t rhs)
{
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs)
        throw std::overflow_error("Vector timing element/slot count overflow");
    return lhs + rhs;
}

std::size_t checkedSizeMultiply(std::size_t lhs, std::size_t rhs)
{
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs)
        throw std::overflow_error("Vector timing lane-slot count overflow");
    return lhs * rhs;
}

bool isVector(TimingOpKind kind)
{
    return kind != TimingOpKind::ScalarALU;
}

bool isMemory(TimingOpKind kind)
{
    return kind == TimingOpKind::VectorLoad || kind == TimingOpKind::VectorStore;
}
} // namespace

VectorTimingModel::VectorTimingModel(TimingConfig config) : config_(config)
{
    if (config_.lanes == 0)
        throw std::invalid_argument("Vector timing model requires at least one lane");
    if (config_.issue_interval_cycles == 0 || config_.scalar_latency_cycles == 0 ||
        config_.vector_group_latency_cycles == 0 || config_.memory_latency_cycles == 0)
        throw std::invalid_argument("Issue interval and operation latencies must be positive");
}

const TimingConfig &VectorTimingModel::config() const noexcept
{
    return config_;
}

TimingSummary VectorTimingModel::simulate(
    const std::vector<TimingInstruction> &instructions) const
{
    TimingSummary summary;
    summary.instruction_count = instructions.size();
    summary.trace.reserve(instructions.size());

    // This is a deterministic, in-order abstract scheduler. Register operands
    // are considered read at issue; destination readiness models RAW/WAW
    // dependencies. A single serialized memory resource is assumed.
    std::unordered_map<std::string, Cycle> register_ready_cycle;
    Cycle next_issue_cycle = 0;
    Cycle memory_ready_cycle = 0;

    for (std::size_t index = 0; index < instructions.size(); ++index)
    {
        const auto &instruction = instructions[index];
        Cycle dependency_ready_cycle = next_issue_cycle;
        for (const auto &source : instruction.sources)
        {
            if (const auto it = register_ready_cycle.find(source);
                it != register_ready_cycle.end())
                dependency_ready_cycle = std::max(dependency_ready_cycle, it->second);
        }
        if (!instruction.destination.empty())
        {
            if (const auto it = register_ready_cycle.find(instruction.destination);
                it != register_ready_cycle.end())
                dependency_ready_cycle = std::max(dependency_ready_cycle, it->second);
        }

        const Cycle after_dependencies = std::max(next_issue_cycle, dependency_ready_cycle);
        const Cycle dependency_stall = after_dependencies - next_issue_cycle;
        Cycle issue_cycle = after_dependencies;
        Cycle memory_stall = 0;
        if (isMemory(instruction.kind))
        {
            issue_cycle = std::max(issue_cycle, memory_ready_cycle);
            memory_stall = issue_cycle - after_dependencies;
        }

        const bool vector_operation = isVector(instruction.kind);
        const std::size_t lane_groups =
            vector_operation && instruction.active_elements != 0
                ? ((instruction.active_elements - 1) / config_.lanes) + 1
                : 0;
        const std::size_t lane_slots = checkedSizeMultiply(lane_groups, config_.lanes);

        Cycle latency = vector_operation ? config_.vector_startup_latency_cycles
                                         : config_.scalar_latency_cycles;
        if (vector_operation)
        {
            latency = checkedAdd(
                latency, checkedMultiply(lane_groups, config_.vector_group_latency_cycles));
            if (instruction.kind == TimingOpKind::VectorReduction)
                latency = checkedAdd(latency, config_.reduction_extra_latency_cycles);
            if (isMemory(instruction.kind))
            {
                latency = checkedAdd(latency, config_.memory_latency_cycles);
                latency = checkedAdd(latency, instruction.extra_memory_delay_cycles);
            }
            if (instruction.active_elements == 0 && latency == 0)
                latency = 1;
        }

        const Cycle start_cycle = checkedAdd(
            issue_cycle, vector_operation ? config_.vector_startup_latency_cycles : 0);
        const Cycle completion_cycle = checkedAdd(issue_cycle, std::max<Cycle>(latency, 1));

        summary.dependency_stall_cycles =
            checkedAdd(summary.dependency_stall_cycles, dependency_stall);
        summary.memory_stall_cycles = checkedAdd(summary.memory_stall_cycles, memory_stall);
        if (vector_operation)
        {
            ++summary.vector_instruction_count;
            summary.active_vector_elements =
                checkedSizeAdd(summary.active_vector_elements, instruction.active_elements);
            summary.lane_slots = checkedSizeAdd(summary.lane_slots, lane_slots);
        }
        summary.estimated_cycles = std::max(summary.estimated_cycles, completion_cycle);

        if (!instruction.destination.empty())
            register_ready_cycle[instruction.destination] = completion_cycle;
        if (isMemory(instruction.kind))
            memory_ready_cycle = completion_cycle;

        summary.trace.push_back({
            index,
            instruction.mnemonic,
            instruction.kind,
            issue_cycle,
            start_cycle,
            completion_cycle,
            instruction.active_elements,
            lane_groups,
            lane_slots,
            dependency_stall,
            memory_stall,
        });

        next_issue_cycle = checkedAdd(issue_cycle, config_.issue_interval_cycles);
    }

    if (summary.lane_slots != 0)
        summary.lane_utilization = static_cast<double>(summary.active_vector_elements) /
                                   static_cast<double>(summary.lane_slots);
    if (summary.estimated_cycles != 0)
        summary.active_elements_per_cycle =
            static_cast<double>(summary.active_vector_elements) /
            static_cast<double>(summary.estimated_cycles);
    return summary;
}

const char *toString(TimingOpKind kind) noexcept
{
    switch (kind)
    {
    case TimingOpKind::ScalarALU: return "scalar";
    case TimingOpKind::VectorALU: return "vector-alu";
    case TimingOpKind::VectorReduction: return "vector-reduction";
    case TimingOpKind::VectorLoad: return "vector-load";
    case TimingOpKind::VectorStore: return "vector-store";
    }
    return "unknown";
}

} // namespace Kites::Performance
