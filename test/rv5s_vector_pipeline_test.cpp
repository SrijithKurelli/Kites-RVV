#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
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


namespace
{
std::array<uint8_t, 16> packSewLanes(unsigned sew, const std::array<uint64_t, 4> &values)
{
    std::array<uint8_t, 16> bytes{};
    const size_t element_bytes = sew / 8u;
    const uint64_t mask = sew == 64 ? UINT64_MAX : ((uint64_t{1} << sew) - 1u);
    for (size_t lane = 0; lane < 4; ++lane)
    {
        const uint64_t value = values[lane] & mask;
        for (size_t byte = 0; byte < element_bytes; ++byte)
            bytes[lane * element_bytes + byte] =
                static_cast<uint8_t>((value >> (8u * byte)) & 0xFFu);
    }
    return bytes;
}

uint64_t readSewLane(const RegisterFile &registers, size_t reg, size_t lane, unsigned sew)
{
    const size_t element_bytes = sew / 8u;
    uint64_t value = 0;
    for (size_t byte = 0; byte < element_bytes; ++byte)
        value |= static_cast<uint64_t>(
                     registers.ReadVectorByte(reg, lane * element_bytes + byte))
                 << (8u * byte);
    return value;
}

unsigned vtypeForSew(unsigned sew)
{
    switch (sew)
    {
    case 8: return 0;
    case 16: return 8;
    case 32: return 16;
    case 64: return 24;
    default: throw std::invalid_argument("unsupported SEW in pipeline test");
    }
}

template <typename Processor>
void runPipelineArithmeticForAllSews()
{
    for (const unsigned sew : {8u, 16u, 32u, 64u})
    {
        std::cerr << "[RV5 all-SEW] begin SEW=" << sew << std::endl;
        setupVmStateDirectory();
        std::cerr << "[RV5 all-SEW] state directory ready SEW=" << sew << std::endl;
        Processor vm;
        std::cerr << "[RV5 all-SEW] processor constructed SEW=" << sew << std::endl;
        vm.registers_.WriteVector(1, packSewLanes(sew, {1, 2, 3, 4}));
        vm.registers_.WriteVector(2, packSewLanes(sew, {2, 3, 4, 5}));
        vm.registers_.WriteVector(6, packSewLanes(sew, {5, 0, 0, 0}));

        std::ostringstream source;
        source << "vsetivli x0, 4, " << vtypeForSew(sew) << "\n"
               << "nop\nnop\n"
               << "vadd.vv v3, v1, v2\n"
               << "vmul.vv v4, v1, v2\n"
               << "nop\nnop\n"
               << "vredsum.vs v5, v4, v6\n"
               << "nop\nnop\n";
        std::cerr << "[RV5 all-SEW] assembling SEW=" << sew << std::endl;
        const auto assembled = assemblePipelineVectorProgram(source.str());
        std::cerr << "[RV5 all-SEW] assembly complete SEW=" << sew << std::endl;
        vm.LoadProgram(assembled);
        std::cerr << "[RV5 all-SEW] program loaded SEW=" << sew << std::endl;
        vm.DebugRun();
        std::cerr << "[RV5 all-SEW] execution complete SEW=" << sew
                  << " actual-SEW=" << static_cast<unsigned>(vm.vectorSew())
                  << " actual-VL=" << static_cast<unsigned>(vm.vectorVl()) << std::endl;

        const size_t vl = std::min<size_t>(4, 128u / sew);
        const uint64_t element_mask =
            sew == 64 ? UINT64_MAX : ((uint64_t{1} << sew) - 1u);
        uint64_t expected_sum = 5;
        for (size_t lane = 0; lane < vl; ++lane)
        {
            const uint64_t lhs = lane + 1u;
            const uint64_t rhs = lane + 2u;
            EXPECT_EQ(readSewLane(vm.registers_, 3, lane, sew), (lhs + rhs) & element_mask)
                << "SEW=" << sew << " lane=" << lane;
            EXPECT_EQ(readSewLane(vm.registers_, 4, lane, sew), (lhs * rhs) & element_mask)
                << "SEW=" << sew << " lane=" << lane;
            expected_sum = (expected_sum + lhs * rhs) & element_mask;
        }
        EXPECT_EQ(readSewLane(vm.registers_, 5, 0, sew), expected_sum)
            << "SEW=" << sew;
        EXPECT_EQ(vm.vectorSew(), sew);
        EXPECT_EQ(vm.vectorVl(), vl);
        std::cerr << "[RV5 all-SEW] assertions complete SEW=" << sew << std::endl;
    }
}

template <typename Processor>
void runPipelineUnitStrideMemoryForAllSews()
{
    for (const unsigned sew : {8u, 16u, 32u, 64u})
    {
        setupVmStateDirectory();
        Processor vm;
        const size_t element_bytes = sew / 8u;
        const size_t vl = std::min<size_t>(4, 128u / sew);
        const size_t active_bytes = element_bytes * vl;
        constexpr uint64_t source_base = 0x100u;
        constexpr uint64_t destination_base = 0x200u;
        vm.registers_.WriteGpr(5, source_base);
        vm.registers_.WriteGpr(6, destination_base);

        for (size_t byte = 0; byte < 16; ++byte)
        {
            vm.memory_controller_.writeByte(source_base + byte,
                                            static_cast<uint8_t>(byte + 1u));
            vm.memory_controller_.writeByte(destination_base + byte, 0xEEu);
        }
        std::array<uint8_t, 16> initial_destination{};
        initial_destination.fill(0xA5u);
        vm.registers_.WriteVector(3, initial_destination);

        std::ostringstream source;
        source << "vsetivli x0, 4, " << vtypeForSew(sew) << "\n"
               << "nop\nnop\n"
               << "vle" << sew << ".v v3, (x5)\n"
               << "nop\nnop\n"
               << "vse" << sew << ".v v3, (x6)\n"
               << "nop\nnop\n";
        vm.LoadProgram(assemblePipelineVectorProgram(source.str()));
        vm.DebugRun();

        for (size_t byte = 0; byte < active_bytes; ++byte)
        {
            const uint8_t expected = static_cast<uint8_t>(byte + 1u);
            EXPECT_EQ(vm.registers_.ReadVectorByte(3, byte), expected)
                << "SEW=" << sew << " byte=" << byte;
            EXPECT_EQ(vm.memory_controller_.readByte(destination_base + byte), expected)
                << "SEW=" << sew << " byte=" << byte;
        }
        for (size_t byte = active_bytes; byte < 16; ++byte)
        {
            EXPECT_EQ(vm.registers_.ReadVectorByte(3, byte), 0xA5u)
                << "SEW=" << sew << " tail byte=" << byte;
            EXPECT_EQ(vm.memory_controller_.readByte(destination_base + byte), 0xEEu)
                << "SEW=" << sew << " inactive memory byte=" << byte;
        }
    }
}

struct PipelineVectorSnapshot
{
    uint64_t pc{0};
    unsigned int cycles{0};
    unsigned int retired{0};
    uint8_t sew{0};
    uint8_t vl{0};
    std::array<std::array<uint8_t, 16>, 32> vectors{};
    std::array<uint8_t, 16> memory{};
};

template <typename Processor>
PipelineVectorSnapshot capturePipelineVectorSnapshot(Processor &vm)
{
    PipelineVectorSnapshot snapshot;
    snapshot.pc = vm.program_counter_;
    snapshot.cycles = vm.cycle_s_;
    snapshot.retired = vm.instructions_retired_;
    snapshot.sew = vm.vectorSew();
    snapshot.vl = vm.vectorVl();
    for (size_t reg = 0; reg < snapshot.vectors.size(); ++reg)
        snapshot.vectors[reg] = vm.registers_.ReadVector(reg);
    for (size_t byte = 0; byte < snapshot.memory.size(); ++byte)
        snapshot.memory[byte] = vm.memory_controller_.readByte(0x200u + byte);
    return snapshot;
}

void expectPipelineVectorSnapshotsEqual(const PipelineVectorSnapshot &actual,
                                        const PipelineVectorSnapshot &expected)
{
    EXPECT_EQ(actual.pc, expected.pc);
    EXPECT_EQ(actual.cycles, expected.cycles);
    EXPECT_EQ(actual.retired, expected.retired);
    EXPECT_EQ(actual.sew, expected.sew);
    EXPECT_EQ(actual.vl, expected.vl);
    EXPECT_EQ(actual.vectors, expected.vectors);
    EXPECT_EQ(actual.memory, expected.memory);
}

template <typename Processor>
void runPipelineVectorUndoRedoRoundTrip()
{
    setupVmStateDirectory();
    Processor vm;
    vm.registers_.WriteGpr(5, 0x100u);
    vm.registers_.WriteGpr(6, 0x200u);
    std::array<uint8_t, 16> source_bytes{};
    for (size_t lane = 0; lane < 4; ++lane)
    {
        const uint32_t value = static_cast<uint32_t>(lane + 1u);
        for (size_t byte = 0; byte < 4; ++byte)
            source_bytes[lane * 4 + byte] =
                static_cast<uint8_t>((value >> (8u * byte)) & 0xFFu);
        for (size_t byte = 0; byte < 4; ++byte)
        {
            vm.memory_controller_.writeByte(0x100u + lane * 4u + byte,
                                            source_bytes[lane * 4 + byte]);
            vm.memory_controller_.writeByte(0x200u + lane * 4u + byte, 0xEEu);
        }
    }
    std::array<uint8_t, 16> initial_v1{};
    initial_v1.fill(0xA5u);
    std::array<uint8_t, 16> initial_v2{};
    initial_v2.fill(0x5Au);
    vm.registers_.WriteVector(1, initial_v1);
    vm.registers_.WriteVector(2, initial_v2);

    vm.LoadProgram(assemblePipelineVectorProgram(R"(
        vsetivli x0, 4, 16
        nop
        nop
        vle32.v v1, (x5)
        nop
        nop
        vmul.vv v2, v1, v1
        nop
        nop
        vse32.v v2, (x6)
        nop
        nop
    )"));

    const PipelineVectorSnapshot before = capturePipelineVectorSnapshot(vm);
    vm.DebugRun();
    const PipelineVectorSnapshot after = capturePipelineVectorSnapshot(vm);
    ASSERT_GT(after.cycles, before.cycles);
    EXPECT_EQ(after.sew, 32u);
    EXPECT_EQ(after.vl, 4u);
    for (size_t lane = 0; lane < 4; ++lane)
    {
        uint32_t value = 0;
        for (size_t byte = 0; byte < 4; ++byte)
            value |= static_cast<uint32_t>(after.vectors[2][lane * 4 + byte]) << (8u * byte);
        EXPECT_EQ(value, static_cast<uint32_t>((lane + 1u) * (lane + 1u)));
    }

    const unsigned int executed_cycles = after.cycles - before.cycles;
    for (unsigned int cycle = 0; cycle < executed_cycles; ++cycle)
        vm.Undo();
    expectPipelineVectorSnapshotsEqual(capturePipelineVectorSnapshot(vm), before);

    for (unsigned int cycle = 0; cycle < executed_cycles; ++cycle)
        vm.Redo();
    expectPipelineVectorSnapshotsEqual(capturePipelineVectorSnapshot(vm), after);
}
} // namespace

TEST(RV5VectorPipeline, HazardDetectionAndForwardingSupportsAllSews)
{
    runPipelineArithmeticForAllSews<RV5StageProcessorHF>();
}
TEST(RV5VectorPipeline, HazardDetectionWithoutForwardingSupportsAllSews)
{
    runPipelineArithmeticForAllSews<RV5StageProcessorHNF>();
}
TEST(RV5VectorPipeline, ForwardingWithoutHazardDetectionSupportsAllSews)
{
    runPipelineArithmeticForAllSews<RV5StageProcessorNHF>();
}
TEST(RV5VectorPipeline, NoHazardDetectionOrForwardingSupportsAllSews)
{
    runPipelineArithmeticForAllSews<RV5StageProcessorNHNF>();
}

TEST(RV5VectorPipeline, HazardDetectionAndForwardingSupportsUnitStrideMemoryAtAllSews)
{
    runPipelineUnitStrideMemoryForAllSews<RV5StageProcessorHF>();
}
TEST(RV5VectorPipeline, HazardDetectionWithoutForwardingSupportsUnitStrideMemoryAtAllSews)
{
    runPipelineUnitStrideMemoryForAllSews<RV5StageProcessorHNF>();
}
TEST(RV5VectorPipeline, ForwardingWithoutHazardDetectionSupportsUnitStrideMemoryAtAllSews)
{
    runPipelineUnitStrideMemoryForAllSews<RV5StageProcessorNHF>();
}
TEST(RV5VectorPipeline, NoHazardDetectionOrForwardingSupportsUnitStrideMemoryAtAllSews)
{
    runPipelineUnitStrideMemoryForAllSews<RV5StageProcessorNHNF>();
}

TEST(RV5VectorPipeline, HazardDetectionAndForwardingVectorMemoryUndoRedoRoundTrip)
{
    runPipelineVectorUndoRedoRoundTrip<RV5StageProcessorHF>();
}
TEST(RV5VectorPipeline, HazardDetectionWithoutForwardingVectorMemoryUndoRedoRoundTrip)
{
    runPipelineVectorUndoRedoRoundTrip<RV5StageProcessorHNF>();
}
TEST(RV5VectorPipeline, ForwardingWithoutHazardDetectionVectorMemoryUndoRedoRoundTrip)
{
    runPipelineVectorUndoRedoRoundTrip<RV5StageProcessorNHF>();
}
TEST(RV5VectorPipeline, NoHazardDetectionOrForwardingVectorMemoryUndoRedoRoundTrip)
{
    runPipelineVectorUndoRedoRoundTrip<RV5StageProcessorNHNF>();
}

} // namespace
