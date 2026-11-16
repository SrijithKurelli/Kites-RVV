#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "utils/utils.h"

namespace
{
using namespace Kites;

std::array<uint8_t, 16> packWords(const std::array<int32_t, 4> &values)
{
    std::array<uint8_t, 16> bytes{};
    for (size_t lane = 0; lane < values.size(); ++lane)
    {
        const uint32_t value = static_cast<uint32_t>(values[lane]);
        for (size_t byte = 0; byte < 4; ++byte)
            bytes[lane * 4 + byte] =
                static_cast<uint8_t>((value >> (8u * byte)) & 0xFFu);
    }
    return bytes;
}

int32_t readWordLane(const RegisterFile &registers, size_t reg, size_t lane)
{
    uint32_t value = 0;
    for (size_t byte = 0; byte < 4; ++byte)
        value |= static_cast<uint32_t>(registers.ReadVectorByte(reg, lane * 4 + byte))
                 << (8u * byte);
    return static_cast<int32_t>(value);
}

AssembledProgram assemblePipelineVectorProgram(const std::string &source)
{
    setupVmStateDirectory();
    std::istringstream stream(source);
    return assemble(stream);
}

const std::string kDependentVectorProgram = R"(
    vsetivli x0, 4, 16
    vmul.vv v3, v1, v2
    vadd.vv v4, v3, v2
    vredsum.vs v5, v4, v7
    vmseq.vv v0, v1, v2
    vadd.vv v8, v1, v2, v0.t
    vcpop.m x10, v0
)";

const std::string kScheduledVectorProgram = R"(
    vsetivli x0, 4, 16
    nop
    nop
    vmul.vv v3, v1, v2
    nop
    nop
    vadd.vv v4, v3, v2
    nop
    nop
    vredsum.vs v5, v4, v7
    nop
    nop
    vmseq.vv v0, v1, v2
    nop
    nop
    vadd.vv v8, v1, v2, v0.t
    nop
    nop
    vcpop.m x10, v0
    nop
    nop
)";

template <typename Processor>
void seedAndRun(Processor &vm, const std::string &program)
{
    setupVmStateDirectory();
    vm.registers_.WriteVector(1, packWords({1, 2, 3, 4}));
    vm.registers_.WriteVector(2, packWords({1, 0, 3, 9}));
    vm.registers_.WriteVector(7, packWords({0, 0, 0, 0}));
    vm.registers_.WriteVector(8, packWords({-1, -1, -1, -1}));
    vm.LoadProgram(assemblePipelineVectorProgram(program));
    vm.DebugRun();
}

template <typename Processor>
void expectPipelineVectorResults(const Processor &vm)
{
    EXPECT_EQ(readWordLane(vm.registers_, 3, 0), 1);
    EXPECT_EQ(readWordLane(vm.registers_, 3, 1), 0);
    EXPECT_EQ(readWordLane(vm.registers_, 3, 2), 9);
    EXPECT_EQ(readWordLane(vm.registers_, 3, 3), 36);

    EXPECT_EQ(readWordLane(vm.registers_, 4, 0), 2);
    EXPECT_EQ(readWordLane(vm.registers_, 4, 1), 0);
    EXPECT_EQ(readWordLane(vm.registers_, 4, 2), 12);
    EXPECT_EQ(readWordLane(vm.registers_, 4, 3), 45);
    EXPECT_EQ(readWordLane(vm.registers_, 5, 0), 59);

    EXPECT_EQ(vm.registers_.ReadVectorByte(0, 0), 0x05u);
    EXPECT_EQ(readWordLane(vm.registers_, 8, 0), 2);
    EXPECT_EQ(readWordLane(vm.registers_, 8, 1), -1);
    EXPECT_EQ(readWordLane(vm.registers_, 8, 2), 6);
    EXPECT_EQ(readWordLane(vm.registers_, 8, 3), -1);
    EXPECT_EQ(vm.registers_.ReadGpr(10), 2u);
}

TEST(RV5VectorPipeline, HazardDetectionAndForwardingExecutesDependentVectorChain)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    seedAndRun(vm, kDependentVectorProgram);
    expectPipelineVectorResults(vm);
}

TEST(RV5VectorPipeline, HazardDetectionWithoutForwardingStallsForVectorDependencies)
{
    setupVmStateDirectory();
    RV5StageProcessorHNF vm;
    seedAndRun(vm, kDependentVectorProgram);
    expectPipelineVectorResults(vm);
}

TEST(RV5VectorPipeline, ForwardingWithoutHazardDetectionForwardsVectorOperands)
{
    setupVmStateDirectory();
    RV5StageProcessorNHF vm;
    seedAndRun(vm, kDependentVectorProgram);
    expectPipelineVectorResults(vm);
}

TEST(RV5VectorPipeline, NoHazardDetectionOrForwardingUsesProgrammerScheduledBubbles)
{
    setupVmStateDirectory();
    RV5StageProcessorNHNF vm;
    seedAndRun(vm, kScheduledVectorProgram);
    expectPipelineVectorResults(vm);
}

namespace
{
template <typename Processor>
void runPipelineVectorMemoryProgram(Processor &vm)
{
    vm.registers_.WriteGpr(5, 0x100u); // Strided source base.
    vm.registers_.WriteGpr(6, 8u);     // Byte stride.
    vm.registers_.WriteGpr(7, 0x200u); // Strided destination base.
    vm.registers_.WriteGpr(8, 8u);
    vm.registers_.WriteGpr(9, 0x300u);  // Unit-stride source base.
    vm.registers_.WriteGpr(10, 0x400u); // Unit-stride destination base.
    vm.registers_.WriteGpr(11, 0x500u); // Masked source.
    vm.registers_.WriteGpr(12, 0x600u); // Masked destination.
    vm.registers_.WriteVectorByte(0, 0, 0x05u); // Active lanes 0 and 2.

    for (size_t lane = 0; lane < 4; ++lane)
    {
        vm.memory_controller_.writeWord(0x100u + lane * 8u,
                                        static_cast<uint32_t>(lane + 1u));
        vm.memory_controller_.writeWord(0x104u + lane * 8u, 0xDEADBEEFu);
        vm.memory_controller_.writeWord(0x200u + lane * 8u, 0xA5A5A5A5u);
        vm.memory_controller_.writeWord(0x204u + lane * 8u, 0xCCCCCCCCu);

        vm.memory_controller_.writeWord(0x300u + lane * 4u,
                                        static_cast<uint32_t>(10u + lane));
        vm.memory_controller_.writeWord(0x400u + lane * 4u, 0xBBBBBBBBu);
        vm.memory_controller_.writeWord(0x500u + lane * 4u,
                                        static_cast<uint32_t>(10u * (lane + 1u)));
        vm.memory_controller_.writeWord(0x600u + lane * 4u, 0x99999999u);
    }
    vm.registers_.WriteVector(5, packWords({-1, -1, -1, -1}));

    vm.LoadProgram(assemblePipelineVectorProgram(R"(
        vsetivli x0, 4, 16
        nop
        nop
        vlse32.v v1, (x5), x6
        nop
        nop
        vadd.vv v2, v1, v1
        nop
        nop
        vsse32.v v2, (x7), x8
        nop
        nop
        vle32.v v3, (x9)
        nop
        nop
        vse32.v v3, (x10)
        nop
        nop
        vle32.v v5, (x11), v0.t
        nop
        nop
        vse32.v v5, (x12), v0.t
        nop
        nop
    )"));
    vm.DebugRun();

    // Strided load/compute/store: padding between active words is untouched.
    for (size_t lane = 0; lane < 4; ++lane)
    {
        EXPECT_EQ(readWordLane(vm.registers_, 1, lane), static_cast<int32_t>(lane + 1u));
        EXPECT_EQ(readWordLane(vm.registers_, 2, lane), static_cast<int32_t>(2u * (lane + 1u)));
        EXPECT_EQ(vm.memory_controller_.readWord(0x200u + lane * 8u),
                  static_cast<uint32_t>(2u * (lane + 1u)));
        EXPECT_EQ(vm.memory_controller_.readWord(0x204u + lane * 8u), 0xCCCCCCCCu);

        // Unit-stride load/store copies four adjacent words.
        EXPECT_EQ(readWordLane(vm.registers_, 3, lane), static_cast<int32_t>(10u + lane));
        EXPECT_EQ(vm.memory_controller_.readWord(0x400u + lane * 4u),
                  static_cast<uint32_t>(10u + lane));
    }

    // Masked load keeps inactive destination lanes unchanged; masked store
    // leaves corresponding memory words untouched.
    EXPECT_EQ(readWordLane(vm.registers_, 5, 0), 10);
    EXPECT_EQ(readWordLane(vm.registers_, 5, 1), -1);
    EXPECT_EQ(readWordLane(vm.registers_, 5, 2), 30);
    EXPECT_EQ(readWordLane(vm.registers_, 5, 3), -1);
    EXPECT_EQ(vm.memory_controller_.readWord(0x600u), 10u);
    EXPECT_EQ(vm.memory_controller_.readWord(0x604u), 0x99999999u);
    EXPECT_EQ(vm.memory_controller_.readWord(0x608u), 30u);
    EXPECT_EQ(vm.memory_controller_.readWord(0x60Cu), 0x99999999u);
}
} // namespace

TEST(RV5VectorPipeline, HazardDetectionAndForwardingSupportsVectorMemory)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    runPipelineVectorMemoryProgram(vm);
}

TEST(RV5VectorPipeline, HazardDetectionWithoutForwardingSupportsVectorMemory)
{
    setupVmStateDirectory();
    RV5StageProcessorHNF vm;
    runPipelineVectorMemoryProgram(vm);
}

TEST(RV5VectorPipeline, ForwardingWithoutHazardDetectionSupportsScheduledVectorMemory)
{
    setupVmStateDirectory();
    RV5StageProcessorNHF vm;
    runPipelineVectorMemoryProgram(vm);
}

TEST(RV5VectorPipeline, NoHazardDetectionOrForwardingSupportsScheduledVectorMemory)
{
    setupVmStateDirectory();
    RV5StageProcessorNHNF vm;
    runPipelineVectorMemoryProgram(vm);
}

} // namespace
