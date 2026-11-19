#include <gtest/gtest.h>

#include <initializer_list>
#include <numeric>
#include <string>
#include <utility>

#include "common/instruction_types.h"
#include "profiler/profiler.h"
#include "utils/to_index.h"

namespace
{
Kites::AssembledProgram makeProgram(
    std::initializer_list<std::pair<unsigned int, std::string>> instructions)
{
    Kites::AssembledProgram program;
    unsigned int instructionIndex = 0;
    for (const auto &[lineNumber, opcode] : instructions)
    {
        Kites::ICUnit instruction;
        instruction.setLineNumber(lineNumber);
        instruction.setInstructionIndex(instructionIndex++);
        instruction.setOpcode(opcode);
        program.intermediate_code.emplace_back(instruction, true);
    }
    return program;
}
} // namespace

TEST(ProfilerTest, ReplacingProgramRebuildsInstructionTypeCounts)
{
    Kites::Profiler profiler;
    const auto firstProgram = makeProgram({{1, "add"}, {2, "addi"}});
    profiler.setLineNumberToInstructionTypeMapping(firstProgram);

    auto counts = profiler.getInstructionTypeCounts();
    EXPECT_EQ(counts[Kites::toIndex(Kites::instruction_set::InstructionType::R_TYPE)], 1u);
    EXPECT_EQ(counts[Kites::toIndex(Kites::instruction_set::InstructionType::I_TYPE)], 1u);

    const auto secondProgram = makeProgram({{8, "vadd.vv"}});
    profiler.setLineNumberToInstructionTypeMapping(secondProgram);

    counts = profiler.getInstructionTypeCounts();
    EXPECT_EQ(counts[Kites::toIndex(Kites::instruction_set::InstructionType::R_TYPE)], 0u);
    EXPECT_EQ(counts[Kites::toIndex(Kites::instruction_set::InstructionType::I_TYPE)], 0u);
    EXPECT_EQ(counts[Kites::toIndex(Kites::instruction_set::InstructionType::VECTOR_TYPE)], 1u);
    EXPECT_EQ(profiler.getInstructionTypeForLine(8),
              Kites::instruction_set::InstructionType::VECTOR_TYPE);
    EXPECT_EQ(profiler.getInstructionTypeForLine(1),
              Kites::instruction_set::InstructionType::UNKNOWN);
}

TEST(ProfilerTest, ResetPreservesInstructionCompositionForLoadedProgram)
{
    Kites::Profiler profiler;
    const auto program = makeProgram({{1, "add"}, {2, "addi"}, {3, "vadd.vv"}});
    profiler.setLineNumberToInstructionTypeMapping(program);

    auto countsBeforeReset = profiler.getInstructionTypeCounts();
    int countRefreshes = 0;
    QObject::connect(&profiler, &Kites::Profiler::updateInstructionTypeCountsSignal,
                     [&countRefreshes]() { ++countRefreshes; });

    profiler.Reset();

    const auto countsAfterReset = profiler.getInstructionTypeCounts();
    EXPECT_EQ(countsAfterReset, countsBeforeReset);
    EXPECT_EQ(countRefreshes, 0);
}

TEST(ProfilerTest, UnknownMnemonicsDoNotContributeToFixedInstructionCountArray)
{
    Kites::Profiler profiler;
    const auto program = makeProgram({{1, "add"}, {2, "not_a_real_instruction"}});
    profiler.setLineNumberToInstructionTypeMapping(program);

    const auto &counts = profiler.getInstructionTypeCounts();
    const size_t classifiedCount = std::accumulate(counts.begin(), counts.end(), size_t{0});
    EXPECT_EQ(classifiedCount, 1u);
    EXPECT_EQ(profiler.getInstructionTypeForLine(2),
              Kites::instruction_set::InstructionType::UNKNOWN);
}
