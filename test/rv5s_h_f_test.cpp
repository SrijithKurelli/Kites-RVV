// #include <gtest/gtest.h>
// #include "processor/rv5s/rv5s_processor_h_f.h"
// #include "assembler/assembler.h"
// #include "utils/utils.h"

// TEST(RV5StageVM_H_F_TEST,hazard_forwarding_test_1)
// {
//     setupVmStateDirectory();
//     RV5StageProcessorHF vm;
//     std::cout << "Program Information " << std::endl;
//     std::cout << "Program size : " <<vm.program_size_ << std::endl;
//     AssembledProgram program = assemble("../examples/pipeline_test_code_1.s");
//     vm.LoadProgram(program);
//     vm.DebugRun();
    
//     GTEST_ASSERT_EQ(vm.registers_.ReadGpr(6), 15);
// }

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include "processor/rv5s/rv5s_processor_base.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "assembler/assembler.h"
#include "utils/utils.h"

using namespace Kites;

static std::unique_ptr<RV5StageProcessorHF> runHazardProgram(const std::string& filename)
{
    setupVmStateDirectory();

    auto vm = std::make_unique<RV5StageProcessorHF>();
    AssembledProgram program = assemble(filename);
    vm->LoadProgram(program);
    vm->DebugRun();
    return vm;
}


// ----------------------------
// 1. R -> R
// ----------------------------

TEST(RV5StageVM_H_F_TEST, r_r_forwarding)
{
    auto vm = runHazardProgram("../examples/R_R_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(6), 5);
}

// ----------------------------
// 2. Load -> Use
// ----------------------------
TEST(RV5StageVM_H_F_TEST, l_u_forwarding)
{
    auto vm = runHazardProgram("../examples/L_U_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(6), 120);
}

// ----------------------------
// 3. R -> Store
// ----------------------------
TEST(RV5StageVM_H_F_TEST, r_s_forwarding)
{
    auto vm = runHazardProgram("../examples/R_S_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(8), 20);
}

// ----------------------------
// 4. Load -> Store
// ----------------------------
TEST(RV5StageVM_H_F_TEST, l_s_forwarding)
{
    auto vm = runHazardProgram("../examples/L_S_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(7), 7);
}

// ----------------------------
// 5. R -> Branch
// ----------------------------
TEST(RV5StageVM_H_F_TEST, r_b_forwarding)
{
    auto vm = runHazardProgram("../examples/R_B_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(10), 0);
}

// ----------------------------
// 6. Load -> Branch
// ----------------------------
TEST(RV5StageVM_H_F_TEST, l_b_forwarding)
{
    auto vm = runHazardProgram("../examples/L_B_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(7), 1);
}

// ----------------------------
// 7. R -> MemAddr
// ----------------------------
TEST(RV5StageVM_H_F_TEST, r_m_forwarding)
{
    auto vm = runHazardProgram("../examples/R_M_hazard.s");
    EXPECT_EQ(vm->registers_.ReadGpr(9), 55);
}

namespace
{
std::array<uint8_t, 16> packVector32(const std::array<int32_t, 4> &values)
{
    std::array<uint8_t, 16> bytes{};
    for (size_t lane = 0; lane < values.size(); ++lane)
    {
        const uint32_t value = static_cast<uint32_t>(values[lane]);
        for (size_t byte = 0; byte < 4; ++byte)
            bytes[lane * 4 + byte] = static_cast<uint8_t>((value >> (byte * 8)) & 0xFFu);
    }
    return bytes;
}

std::array<int32_t, 4> unpackVector32(const std::array<uint8_t, 16> &bytes)
{
    std::array<int32_t, 4> values{};
    for (size_t lane = 0; lane < values.size(); ++lane)
    {
        uint32_t value = 0;
        for (size_t byte = 0; byte < 4; ++byte)
            value |= static_cast<uint32_t>(bytes[lane * 4 + byte]) << (byte * 8);
        values[lane] = static_cast<int32_t>(value);
    }
    return values;
}
} // namespace

TEST(RV5StageVM_H_F_TEST, vectorSubsetExecutesWithMaskAndVectorDependencyStalls)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    std::array<uint8_t, 16> predicate{};
    predicate[0] = 0b00000101; // mask lanes 0 and 2
    vm.registers_.WriteVector(0, predicate);
    vm.registers_.WriteVector(1, packVector32({1, 2, 3, 4}));
    vm.registers_.WriteVector(2, packVector32({10, 20, 30, 40}));
    vm.registers_.WriteVector(3, packVector32({-5, -5, -5, -5}));
    vm.registers_.WriteVector(4, packVector32({-7, -7, -7, -7}));

    const auto program = assemble("../examples/vector/vector_pipeline_hf_subset.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    EXPECT_EQ(vm.registers_.ReadGpr(5), 4u);
    EXPECT_EQ(unpackVector32(vm.registers_.ReadVector(3)),
              (std::array<int32_t, 4>{11, -5, 33, -5}));
    EXPECT_EQ(unpackVector32(vm.registers_.ReadVector(4)),
              (std::array<int32_t, 4>{110, -7, 990, -7}));
    EXPECT_EQ(unpackVector32(vm.registers_.ReadVector(5)),
              (std::array<int32_t, 4>{109, -9, 987, -11}));
}

TEST(RV5StageVM_H_F_TEST, vectorRegisterWritebackSupportsUndoAndRedo)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    const auto initial = packVector32({-5, -5, -5, -5});
    const auto expected = packVector32({11, 22, 33, 44});
    vm.registers_.WriteVector(1, packVector32({1, 2, 3, 4}));
    vm.registers_.WriteVector(2, packVector32({10, 20, 30, 40}));
    vm.registers_.WriteVector(3, initial);

    const auto program = assemble("../examples/vector/vector_pipeline_hf_undo.s");
    vm.LoadProgram(program);

    bool committed = false;
    for (int cycle = 0; cycle < 16; ++cycle)
    {
        vm.Step();
        if (vm.registers_.ReadVector(3) == expected)
        {
            committed = true;
            break;
        }
    }
    ASSERT_TRUE(committed);
    vm.Undo();
    EXPECT_EQ(vm.registers_.ReadVector(3), initial);
    vm.Redo();
    EXPECT_EQ(vm.registers_.ReadVector(3), expected);
}

TEST(RV5StageVM_H_F_TEST, vectorConfigAndScalarMultiplyForwardGprOperands)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    vm.registers_.WriteVector(1, packVector32({1, -2, 3, -4}));
    vm.registers_.WriteVector(2, packVector32({10, 10, 10, 10}));

    const auto program = assemble("../examples/vector/vector_pipeline_hf_gpr_forwarding.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    // x7 supplies AVL directly after its producing ADDI; x5 receives VL from
    // vsetvli and is consumed by the next ADDI; x6 supplies vmul.vx directly.
    EXPECT_EQ(vm.registers_.ReadGpr(5), 4u);
    EXPECT_EQ(vm.registers_.ReadGpr(8), 5u);
    EXPECT_EQ(unpackVector32(vm.registers_.ReadVector(3)),
              (std::array<int32_t, 4>{3, -6, 9, -12}));
    EXPECT_EQ(unpackVector32(vm.registers_.ReadVector(4)),
              (std::array<int32_t, 4>{13, 4, 19, -2}));
}
