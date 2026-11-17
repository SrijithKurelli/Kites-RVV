#include <gtest/gtest.h>

#include "profiler/vector_timing_model.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using Kites::Performance::TimingConfig;
using Kites::Performance::TimingInstruction;
using Kites::Performance::TimingOpKind;
using Kites::Performance::VectorTimingModel;
}

TEST(VectorTimingModel, ComputesLaneGroupsAndUtilisation)
{
    TimingConfig config;
    config.lanes = 2;
    config.vector_startup_latency_cycles = 2;
    config.vector_group_latency_cycles = 1;
    const VectorTimingModel model(config);

    const auto summary = model.simulate({
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 5},
    });

    ASSERT_EQ(summary.trace.size(), 1u);
    EXPECT_EQ(summary.trace[0].lane_groups, 3u);
    EXPECT_EQ(summary.trace[0].lane_slots, 6u);
    EXPECT_EQ(summary.trace[0].completion_cycle, 5u);
    EXPECT_EQ(summary.estimated_cycles, 5u);
    EXPECT_EQ(summary.active_vector_elements, 5u);
    EXPECT_NEAR(summary.lane_utilization, 5.0 / 6.0, 1e-9);
}

TEST(VectorTimingModel, ReportsReadAfterWriteDependencyStalls)
{
    TimingConfig config;
    config.issue_interval_cycles = 1;
    config.scalar_latency_cycles = 3;
    const VectorTimingModel model(config);

    const auto summary = model.simulate({
        {"add", TimingOpKind::ScalarALU, "x1", {"x2", "x3"}},
        {"add", TimingOpKind::ScalarALU, "x4", {"x1", "x5"}},
    });

    ASSERT_EQ(summary.trace.size(), 2u);
    EXPECT_EQ(summary.trace[0].issue_cycle, 0u);
    EXPECT_EQ(summary.trace[0].completion_cycle, 3u);
    EXPECT_EQ(summary.trace[1].issue_cycle, 3u);
    EXPECT_EQ(summary.trace[1].dependency_stall_cycles, 2u);
    EXPECT_EQ(summary.dependency_stall_cycles, 2u);
    EXPECT_EQ(summary.estimated_cycles, 6u);
}

TEST(VectorTimingModel, SerializesMemoryOperationsAndCountsMemoryStalls)
{
    TimingConfig config;
    config.lanes = 2;
    config.vector_startup_latency_cycles = 0;
    config.vector_group_latency_cycles = 1;
    config.memory_latency_cycles = 4;
    const VectorTimingModel model(config);

    const auto summary = model.simulate({
        {"vle32.v", TimingOpKind::VectorLoad, "v1", {"x5"}, 2},
        {"vle32.v", TimingOpKind::VectorLoad, "v2", {"x6"}, 2},
    });

    ASSERT_EQ(summary.trace.size(), 2u);
    EXPECT_EQ(summary.trace[0].completion_cycle, 5u);
    EXPECT_EQ(summary.trace[1].issue_cycle, 5u);
    EXPECT_EQ(summary.trace[1].memory_stall_cycles, 4u);
    EXPECT_EQ(summary.memory_stall_cycles, 4u);
    EXPECT_EQ(summary.estimated_cycles, 10u);
}

TEST(VectorTimingModel, MoreLanesReduceVectorExecutionTime)
{
    TimingConfig narrow;
    narrow.lanes = 1;
    narrow.vector_startup_latency_cycles = 1;
    narrow.vector_group_latency_cycles = 1;
    TimingConfig wide = narrow;
    wide.lanes = 4;

    const std::vector<TimingInstruction> program = {
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 8},
    };

    const auto narrow_result = VectorTimingModel(narrow).simulate(program);
    const auto wide_result = VectorTimingModel(wide).simulate(program);
    EXPECT_EQ(narrow_result.estimated_cycles, 9u);
    EXPECT_EQ(wide_result.estimated_cycles, 3u);
    EXPECT_GT(narrow_result.estimated_cycles, wide_result.estimated_cycles);
}

TEST(VectorTimingModel, RejectsInvalidLaneConfiguration)
{
    TimingConfig config;
    config.lanes = 0;
    EXPECT_THROW(static_cast<void>(VectorTimingModel{config}), std::invalid_argument);
}

TEST(VectorTimingModel, EmptyProgramHasZeroCyclesAndNoLaneUtilisation)
{
    const auto summary = VectorTimingModel().simulate({});
    EXPECT_EQ(summary.instruction_count, 0u);
    EXPECT_EQ(summary.estimated_cycles, 0u);
    EXPECT_EQ(summary.active_vector_elements, 0u);
    EXPECT_DOUBLE_EQ(summary.lane_utilization, 0.0);
}

TEST(VectorTimingModel, TracksVectorToReductionDependencies)
{
    TimingConfig config;
    config.lanes = 2;
    config.vector_startup_latency_cycles = 2;
    config.vector_group_latency_cycles = 1;
    config.reduction_extra_latency_cycles = 1;
    const VectorTimingModel model(config);

    const auto summary = model.simulate({
        {"vmul.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 4},
        {"vredsum.vs", TimingOpKind::VectorReduction, "x10", {"v3", "v0"}, 4},
    });

    ASSERT_EQ(summary.trace.size(), 2u);
    EXPECT_EQ(summary.trace[0].completion_cycle, 4u);
    EXPECT_EQ(summary.trace[1].issue_cycle, 4u);
    EXPECT_EQ(summary.trace[1].dependency_stall_cycles, 3u);
    EXPECT_EQ(summary.estimated_cycles, 9u);
}

TEST(VectorTimingModel, AddsConfiguredExtraMemoryDelay)
{
    TimingConfig config;
    config.lanes = 2;
    config.vector_startup_latency_cycles = 0;
    config.vector_group_latency_cycles = 1;
    config.memory_latency_cycles = 4;
    const VectorTimingModel model(config);

    const auto summary = model.simulate({
        {"vle32.v", TimingOpKind::VectorLoad, "v1", {"x5"}, 2, 7},
    });

    ASSERT_EQ(summary.trace.size(), 1u);
    EXPECT_EQ(summary.trace[0].completion_cycle, 12u);
    EXPECT_EQ(summary.estimated_cycles, 12u);
}


TEST(VectorTimingModel, RejectsZeroRequiredTimingParameters)
{
    TimingConfig config;
    config.issue_interval_cycles = 0;
    EXPECT_THROW(static_cast<void>(VectorTimingModel{config}), std::invalid_argument);

    config = TimingConfig{};
    config.scalar_latency_cycles = 0;
    EXPECT_THROW(static_cast<void>(VectorTimingModel{config}), std::invalid_argument);

    config = TimingConfig{};
    config.vector_group_latency_cycles = 0;
    EXPECT_THROW(static_cast<void>(VectorTimingModel{config}), std::invalid_argument);

    config = TimingConfig{};
    config.memory_latency_cycles = 0;
    EXPECT_THROW(static_cast<void>(VectorTimingModel{config}), std::invalid_argument);

    config = TimingConfig{};
    config.vector_startup_latency_cycles = 0;
    config.reduction_extra_latency_cycles = 0;
    EXPECT_NO_THROW(static_cast<void>(VectorTimingModel{config}));
}

TEST(VectorTimingModel, RejectsPerInstructionLaneSlotOverflow)
{
    TimingConfig config;
    config.lanes = std::numeric_limits<std::size_t>::max() / 2 + 1;
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"},
         std::numeric_limits<std::size_t>::max()},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}

TEST(VectorTimingModel, RejectsAccumulatedLaneSlotOverflow)
{
    TimingConfig config;
    config.lanes = std::numeric_limits<std::size_t>::max() / 2 + 1;
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 1},
        {"vadd.vv", TimingOpKind::VectorALU, "v4", {"v5", "v6"}, 1},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}


TEST(VectorTimingModel, RejectsVectorLatencyAdditionOverflow)
{
    TimingConfig config;
    config.lanes = 1;
    config.vector_startup_latency_cycles = std::numeric_limits<std::uint64_t>::max();
    config.vector_group_latency_cycles = 1;
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 1},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}

TEST(VectorTimingModel, RejectsLaneGroupLatencyMultiplicationOverflow)
{
    TimingConfig config;
    config.lanes = 1;
    config.vector_startup_latency_cycles = 0;
    config.vector_group_latency_cycles =
        std::numeric_limits<std::uint64_t>::max() / 2 + 1;
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 2},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}

TEST(VectorTimingModel, RejectsCompletionCycleOverflow)
{
    TimingConfig config;
    config.scalar_latency_cycles = std::numeric_limits<std::uint64_t>::max();
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"add", TimingOpKind::ScalarALU, "x1", {}},
        {"add", TimingOpKind::ScalarALU, "x2", {}},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}

TEST(VectorTimingModel, RejectsIssueIntervalCompletionOverflow)
{
    TimingConfig config;
    config.issue_interval_cycles = std::numeric_limits<std::uint64_t>::max();
    config.scalar_latency_cycles = 1;
    const VectorTimingModel model(config);
    const std::vector<TimingInstruction> program = {
        {"add", TimingOpKind::ScalarALU, "x1", {}},
        {"add", TimingOpKind::ScalarALU, "x2", {}},
    };
    EXPECT_THROW(model.simulate(program), std::overflow_error);
}
