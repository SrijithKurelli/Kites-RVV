#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>

#include "assembler/assembler.h"
#include "common/instructions.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

namespace
{
Kites::AssembledProgram assembleVectorSource(const std::string &source)
{
    Kites::setupVmStateDirectory();
    std::istringstream stream(source);
    return Kites::assemble(stream);
}

void writeLanes(Kites::RegisterFile &registers, size_t reg, const std::initializer_list<uint8_t> &values)
{
    size_t byte = 0;
    for (uint8_t value : values)
        registers.WriteVectorByte(reg, byte++, value);
}

void writeElement(Kites::RegisterFile &registers, size_t reg, size_t lane,
                  size_t element_bytes, uint64_t value)
{
    const size_t offset = lane * element_bytes;
    for (size_t byte = 0; byte < element_bytes; ++byte)
        registers.WriteVectorByte(reg, offset + byte,
                                  static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu));
}

uint64_t readElement(const Kites::RegisterFile &registers, size_t reg,
                     size_t lane, size_t element_bytes)
{
    const size_t offset = lane * element_bytes;
    uint64_t value = 0;
    for (size_t byte = 0; byte < element_bytes; ++byte)
        value |= static_cast<uint64_t>(registers.ReadVectorByte(reg, offset + byte)) << (8 * byte);
    return value;
}
}

TEST(VectorExtension, ProcessorManagerExposesVectorConfigurationForCli)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    EXPECT_TRUE(manager.hasVectorState());
    EXPECT_EQ(manager.getVectorSew(), 8u);
    EXPECT_EQ(manager.getVectorVl(), 0u);

    manager.loadProgram(assembleVectorSource(
        "addi x5, x0, 4\n"
        "vsetvli x6, x5, 16\n"
        "nop\n"));
    manager.step();
    manager.step();

    EXPECT_EQ(manager.getVectorSew(), 32u);
    EXPECT_EQ(manager.getVectorVl(), 4u);
}

TEST(VectorExtension, AverageFromMaskedSumAndCountUsesIntegerTruncation)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    auto *registers = manager.getRegisterFile();

    writeElement(*registers, 1, 0, 4, 1);
    writeElement(*registers, 1, 1, 4, 2);
    writeElement(*registers, 1, 2, 4, 4);
    writeElement(*registers, 1, 3, 4, 4);
    writeElement(*registers, 2, 0, 4, 1);
    writeElement(*registers, 2, 1, 4, 0);
    writeElement(*registers, 2, 2, 4, 4);
    writeElement(*registers, 2, 3, 4, 0);
    writeElement(*registers, 4, 0, 4, 0);

    manager.loadProgram(assembleVectorSource(
        "addi x6, x0, 4\n"
        "addi x10, x0, 256\n"
        "vsetvli x7, x6, 16\n"
        "vmseq.vv v0, v1, v2\n"
        "vredsum.vs v5, v1, v4, v0.t\n"
        "vse32.v v5, (x10)\n"
        "lw x11, 0(x10)\n"
        "vcpop.m x12, v0\n"
        "div x13, x11, x12\n"
        "nop\n"));

    for (int instruction = 0; instruction < 9; ++instruction)
        manager.step();

    EXPECT_EQ(registers->ReadGpr(11), 5u);
    EXPECT_EQ(registers->ReadGpr(12), 2u);
    EXPECT_EQ(registers->ReadGpr(13), 2u); // 5 / 2 truncates toward zero.
    EXPECT_EQ(readElement(*registers, 5, 0, 4), 5u);
}

TEST(VectorExtension, AverageExampleHandlesZeroMatchingElements)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    auto *registers = manager.getRegisterFile();

    writeElement(*registers, 1, 0, 4, 1);
    writeElement(*registers, 1, 1, 4, 2);
    writeElement(*registers, 1, 2, 4, 3);
    writeElement(*registers, 1, 3, 4, 4);
    writeElement(*registers, 2, 0, 4, 9);
    writeElement(*registers, 2, 1, 4, 9);
    writeElement(*registers, 2, 2, 4, 9);
    writeElement(*registers, 2, 3, 4, 9);

    manager.loadProgram(assembleVectorSource(
        "addi x6, x0, 4\n"
        "vsetvli x7, x6, 16\n"
        "vmseq.vv v0, v1, v2\n"
        "vcpop.m x12, v0\n"
        "beq x12, x0, no_matches\n"
        "addi x13, x0, -1\n"
        "jal x0, done\n"
        "no_matches:\n"
        "addi x13, x0, 0\n"
        "done:\n"
        "nop\n"));

    for (int instruction = 0; instruction < 7; ++instruction)
        manager.step();

    EXPECT_EQ(registers->ReadGpr(12), 0u);
    EXPECT_EQ(registers->ReadGpr(13), 0u);
}

TEST(VectorExtension, RegisterFileStoresAndResets128Bits)
{
    Kites::RegisterFile registers;
    for (size_t i = 0; i < 16; ++i)
        registers.WriteVectorByte(3, i, static_cast<uint8_t>(i + 1));
    EXPECT_EQ(registers.ReadVectorByte(3, 0), 1u);
    EXPECT_EQ(registers.ReadVectorByte(3, 15), 16u);
    registers.Reset();
    for (size_t i = 0; i < 16; ++i)
        EXPECT_EQ(registers.ReadVectorByte(3, i), 0u);
}

TEST(VectorExtension, AssemblerEncodesVsetvliAndVectorArithmetic)
{
    auto program = assembleVectorSource("vsetvli x5, x6, 16\nvadd.vv v1, v2, v3\nvsub.vv v4, v5, v6\n");
    ASSERT_EQ(program.text_buffer.size(), 3u);
    EXPECT_EQ(program.text_buffer[0], 0x010372D7u);
    EXPECT_EQ(program.text_buffer[1], 0x022180D7u);
    EXPECT_EQ(program.text_buffer[2], 0x0A530257u);
}

TEST(VectorExtension, AssemblerEncodesUnitStrideVectorLoadsAndStores)
{
    auto program = assembleVectorSource(
        "vle8.v v1, (x5)\n"
        "vle16.v v1, (x5)\n"
        "vle32.v v1, (x5)\n"
        "vle64.v v1, (x5)\n"
        "vse8.v v1, (x6)\n"
        "vse16.v v1, (x6)\n"
        "vse32.v v1, (x6)\n"
        "vse64.v v1, (x6)\n");
    ASSERT_EQ(program.text_buffer.size(), 8u);
    EXPECT_EQ(program.text_buffer[0], 0x02028087u);
    EXPECT_EQ(program.text_buffer[1], 0x0202D087u);
    EXPECT_EQ(program.text_buffer[2], 0x0202E087u);
    EXPECT_EQ(program.text_buffer[3], 0x0202F087u);
    EXPECT_EQ(program.text_buffer[4], 0x020300A7u);
    EXPECT_EQ(program.text_buffer[5], 0x020350A7u);
    EXPECT_EQ(program.text_buffer[6], 0x020360A7u);
    EXPECT_EQ(program.text_buffer[7], 0x020370A7u);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]), "vle8.v v1, (x5)");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[7]), "vse64.v v1, (x6)");
}

TEST(VectorExtension, VectorUnitStrideMemoryWorksForEverySupportedSew)
{
    struct Case
    {
        int zimm;
        size_t element_bytes;
        const char *load_mnemonic;
        const char *store_mnemonic;
    };
    const Case cases[] = {
        {0, 1, "vle8.v", "vse8.v"},
        {8, 2, "vle16.v", "vse16.v"},
        {16, 4, "vle32.v", "vse32.v"},
        {24, 8, "vle64.v", "vse64.v"},
    };

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        constexpr uint64_t source_address = 0x1000;
        constexpr uint64_t destination_address = 0x2000;
        constexpr uint8_t kInitialVectorByte = 0xCC;
        constexpr uint8_t kInitialMemoryByte = 0xA0;

        vm.registers_.WriteGpr(5, 2); // Two active vector elements.
        vm.registers_.WriteGpr(7, source_address);
        vm.registers_.WriteGpr(8, destination_address);

        for (size_t byte = 0; byte < 16; ++byte)
        {
            vm.registers_.WriteVectorByte(1, byte, kInitialVectorByte);
            vm.memory_controller_.writeByte(source_address + byte,
                                            static_cast<uint8_t>(byte + 1));
            vm.memory_controller_.writeByte(destination_address + byte,
                                            static_cast<uint8_t>(kInitialMemoryByte + byte));
        }

        const std::string source =
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n" +
            std::string(test_case.load_mnemonic) + " v1, (x7)\n" +
            std::string(test_case.store_mnemonic) + " v1, (x8)\n";
        vm.LoadProgram(assembleVectorSource(source));
        vm.Step(); // vsetvli
        vm.Step(); // vle*.v

        const size_t active_bytes = 2 * test_case.element_bytes;
        for (size_t byte = 0; byte < active_bytes; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte),
                      static_cast<uint8_t>(byte + 1));
        for (size_t byte = active_bytes; byte < 16; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), kInitialVectorByte);

        vm.Step(); // vse*.v
        for (size_t byte = 0; byte < active_bytes; ++byte)
            EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                      static_cast<uint8_t>(byte + 1));
        for (size_t byte = active_bytes; byte < 16; ++byte)
            EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                      static_cast<uint8_t>(kInitialMemoryByte + byte));

        vm.Undo(); // Store undo restores previous memory.
        for (size_t byte = 0; byte < 16; ++byte)
            EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                      static_cast<uint8_t>(kInitialMemoryByte + byte));

        vm.Undo(); // Load undo restores the original vector destination.
        for (size_t byte = 0; byte < 16; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), kInitialVectorByte);

        vm.Redo();
        for (size_t byte = 0; byte < active_bytes; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte),
                      static_cast<uint8_t>(byte + 1));
        vm.Redo();
        for (size_t byte = 0; byte < active_bytes; ++byte)
            EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                      static_cast<uint8_t>(byte + 1));
    }
}

TEST(VectorExtension, VectorMemoryRejectsMismatchedEew)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 1);
    vm.registers_.WriteGpr(7, 0x1000);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n" // SEW=8
        "vle16.v v1, (x7)\n"));
    vm.Step();
    EXPECT_THROW(vm.Step(), std::runtime_error);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 0u);
}

TEST(VectorExtension, VectorAddUsesCurrentVlAndSew)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    writeLanes(vm.registers_, 2, {1, 2, 3, 4, 5, 6, 7, 8});
    writeLanes(vm.registers_, 3, {10, 20, 30, 40, 50, 60, 70, 80});
    vm.LoadProgram(assembleVectorSource("vsetvli x6, x5, 0\nvadd.vv v1, v2, v3\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 11u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 1), 22u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 2), 33u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 3), 44u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 4), 0u); // tail-undisturbed initial state
}

TEST(VectorExtension, VectorSubWrapsWithinElementWidth)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 2);
    writeLanes(vm.registers_, 2, {1, 0});
    writeLanes(vm.registers_, 3, {2, 1});
    vm.LoadProgram(assembleVectorSource("vsetvli x6, x5, 0\nvsub.vv v1, v2, v3\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 255u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 1), 255u);
}

TEST(VectorExtension, VsetvliUsesArchitecturalX0AvlRules)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 8\n"  // SEW=16, VL=4
        "vsetvli x0, x0, 0\n" // SEW=8; preserve the current VL=4
        ""));
    vm.Step();
    ASSERT_EQ(vm.vector_vl_, 4u);
    EXPECT_EQ(vm.registers_.ReadGpr(6), 4u);
    vm.Step();
    EXPECT_EQ(vm.vector_vl_, 4u); // rs1=x0, rd=x0 preserves current vl
    EXPECT_EQ(vm.vector_sew_, 8u);

    vm.Reset();
    vm.LoadProgram(assembleVectorSource("vsetvli x6, x0, 8\n"));
    vm.Step();
    EXPECT_EQ(vm.vector_vl_, 8u); // rd!=x0 requests VLMAX for SEW=16
    EXPECT_EQ(vm.registers_.ReadGpr(6), 8u);
}

TEST(VectorExtension, VectorStateAndResultsSurviveUndoRedo)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 2);
    writeLanes(vm.registers_, 2, {9, 10});
    writeLanes(vm.registers_, 3, {2, 3});
    vm.LoadProgram(assembleVectorSource("vsetvli x6, x5, 0\nvadd.vv v1, v2, v3\n"));

    vm.Step();
    ASSERT_EQ(vm.vector_vl_, 2u);
    EXPECT_EQ(vm.registers_.ReadGpr(6), 2u);
    vm.Step();
    ASSERT_EQ(vm.registers_.ReadVectorByte(1, 0), 11u);
    ASSERT_EQ(vm.registers_.ReadVectorByte(1, 1), 13u);

    vm.Undo();
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 0u);
    EXPECT_EQ(vm.vector_vl_, 2u);
    vm.Redo();
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 11u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 1), 13u);

    vm.Undo();
    vm.Undo();
    EXPECT_EQ(vm.vector_vl_, 0u);
    EXPECT_EQ(vm.vector_sew_, 8u);
    EXPECT_EQ(vm.registers_.ReadGpr(6), 0u);
}

TEST(VectorExtension, VsetvliComputesVlmaxForEverySupportedSew)
{
    const struct Case { int zimm; uint64_t vlmax; } cases[] = {
        {0, 16}, {8, 8}, {16, 4}, {24, 2}
    };
    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.LoadProgram(assembleVectorSource("vsetvli x6, x0, " +
                                            std::to_string(test_case.zimm) + "\n"));
        vm.Step();
        EXPECT_EQ(vm.vector_vl_, test_case.vlmax);
    }
}

TEST(VectorExtension, ArithmeticWorksForEverySupportedSew)
{
    struct Case
    {
        int zimm;
        size_t element_bytes;
        uint64_t lhs;
        uint64_t rhs;
        uint64_t sum;
        uint64_t difference;
    };
    const Case cases[] = {
        {0, 1, 0xFEULL, 5ULL, 3ULL, 0xF9ULL},
        {8, 2, 0xFFFEULL, 5ULL, 3ULL, 0xFFF9ULL},
        {16, 4, 0xFFFFFFFEULL, 5ULL, 3ULL, 0xFFFFFFF9ULL},
        {24, 8, 0xFFFFFFFFFFFFFFFEULL, 5ULL, 3ULL, 0xFFFFFFFFFFFFFFF9ULL},
    };

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.registers_.WriteGpr(5, 2);
        writeElement(vm.registers_, 2, 0, test_case.element_bytes, test_case.lhs);
        writeElement(vm.registers_, 3, 0, test_case.element_bytes, test_case.rhs);
        writeElement(vm.registers_, 2, 1, test_case.element_bytes, test_case.rhs);
        writeElement(vm.registers_, 3, 1, test_case.element_bytes, test_case.lhs);

        const std::string source =
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n"
            "vadd.vv v1, v2, v3\n"
            "vsub.vv v4, v2, v3\n";
        vm.LoadProgram(assembleVectorSource(source));
        vm.Step();
        vm.Step();
        vm.Step();

        EXPECT_EQ(readElement(vm.registers_, 1, 0, test_case.element_bytes), test_case.sum);
        EXPECT_EQ(readElement(vm.registers_, 4, 0, test_case.element_bytes),
                  test_case.difference);
        EXPECT_EQ(readElement(vm.registers_, 1, 1, test_case.element_bytes), test_case.sum);
        const uint64_t expected_reverse_difference =
            test_case.element_bytes == sizeof(uint64_t)
                ? test_case.rhs - test_case.lhs
                : (test_case.rhs - test_case.lhs) &
                      ((uint64_t{1} << (test_case.element_bytes * 8)) - 1);
        EXPECT_EQ(readElement(vm.registers_, 4, 1, test_case.element_bytes),
                  expected_reverse_difference);
    }
}

TEST(VectorExtension, X0AvlFormRejectsAnyVLMAXReduction)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"  // SEW=8, VLMAX=16, VL=4
        "vsetvli x0, x0, 8\n" // SEW=16, VLMAX=8: reduction is reserved even though VL fits
        ""));
    vm.Step();
    ASSERT_EQ(vm.vector_vl_, 4u);
    ASSERT_EQ(vm.vector_sew_, 8u);
    EXPECT_THROW(vm.Step(), std::runtime_error);
    EXPECT_EQ(vm.vector_vl_, 4u);
    EXPECT_EQ(vm.vector_sew_, 8u);
}

TEST(VectorExtension, ReservedX0AvlFormDoesNotPartiallyChangeConfiguration)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 12);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"  // SEW=8, VL=12
        "vsetvli x0, x0, 24\n" // SEW=64, VLMAX=2: reserved for current VL=12
        ""));
    vm.Step();
    ASSERT_EQ(vm.vector_vl_, 12u);
    EXPECT_THROW(vm.Step(), std::runtime_error);
    EXPECT_EQ(vm.vector_sew_, 8u);
    EXPECT_EQ(vm.vector_vl_, 12u);
}

TEST(VectorExtension, AssemblerEncodesReductionsAndMaskPopulationCount)
{
    const auto program = assembleVectorSource(
        "vredsum.vs v1, v2, v3\n"
        "vredand.vs v4, v2, v3\n"
        "vredor.vs v5, v2, v3\n"
        "vredxor.vs v6, v2, v3\n"
        "vredmin.vs v7, v2, v3\n"
        "vredmax.vs v8, v2, v3\n"
        "vcpop.m x9, v4\n");
    ASSERT_EQ(program.text_buffer.size(), 7u);
    const auto reduction = [](uint32_t funct6, uint32_t vd)
    {
        return (funct6 << 26) | (1u << 25) | (2u << 20) | (3u << 15) |
               (0b010u << 12) | (vd << 7) | 0x57u;
    };
    EXPECT_EQ(program.text_buffer[0], reduction(0b000000, 1));
    EXPECT_EQ(program.text_buffer[1], reduction(0b000001, 4));
    EXPECT_EQ(program.text_buffer[2], reduction(0b000010, 5));
    EXPECT_EQ(program.text_buffer[3], reduction(0b000011, 6));
    EXPECT_EQ(program.text_buffer[4], reduction(0b000101, 7));
    EXPECT_EQ(program.text_buffer[5], reduction(0b000111, 8));
    EXPECT_EQ(program.text_buffer[6],
              (0b010000u << 26) | (1u << 25) | (4u << 20) | (0b10000u << 15) |
                  (0b110u << 12) | (9u << 7) | 0x57u);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]),
              "vredsum.vs v1, v2, v3");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[6]),
              "vcpop.m x9, v4");
}

TEST(VectorExtension, ReductionsWorkForEverySupportedSew)
{
    const struct Case { int zimm; size_t bytes; size_t lanes; } cases[] = {
        {0, 1, 3}, {8, 2, 3}, {16, 4, 3}, {24, 8, 2}
    };
    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.registers_.WriteGpr(5, 3);
        writeElement(vm.registers_, 3, 0, test_case.bytes, 10);
        for (size_t lane = 0; lane < test_case.lanes; ++lane)
            writeElement(vm.registers_, 2, lane, test_case.bytes, lane + 1);
        for (size_t reg : {4u, 5u, 6u, 7u, 8u, 9u})
            for (size_t byte = 0; byte < 16; ++byte)
                vm.registers_.WriteVectorByte(reg, byte, 0xAAu);
        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n"
            "vredsum.vs v4, v2, v3\n"
            "vredand.vs v5, v2, v3\n"
            "vredor.vs v6, v2, v3\n"
            "vredxor.vs v7, v2, v3\n"
            "vredmin.vs v8, v2, v3\n"
            "vredmax.vs v9, v2, v3\n"));
        for (int step = 0; step < 7; ++step)
            vm.Step();
        const uint64_t expected_sum = test_case.lanes == 2 ? 13u : 16u;
        const uint64_t expected_xor = test_case.lanes == 2 ? 9u : 10u;
        EXPECT_EQ(readElement(vm.registers_, 4, 0, test_case.bytes), expected_sum);
        EXPECT_EQ(readElement(vm.registers_, 5, 0, test_case.bytes), 0u);
        EXPECT_EQ(readElement(vm.registers_, 6, 0, test_case.bytes), 11u);
        EXPECT_EQ(readElement(vm.registers_, 7, 0, test_case.bytes), expected_xor);
        EXPECT_EQ(readElement(vm.registers_, 8, 0, test_case.bytes), 1u);
        EXPECT_EQ(readElement(vm.registers_, 9, 0, test_case.bytes), 10u);
        const uint64_t tail_mask = test_case.bytes == 8 ? UINT64_MAX :
            ((uint64_t{1} << (test_case.bytes * 8)) - 1u);
        if (test_case.lanes * test_case.bytes < 16)
        {
            const uint64_t expected_tail = UINT64_C(0xAAAAAAAAAAAAAAAA) & tail_mask;
            for (size_t reg : {4u, 5u, 6u, 7u, 8u, 9u})
                EXPECT_EQ(readElement(vm.registers_, reg, test_case.lanes, test_case.bytes),
                          expected_tail);
        }
    }
}

TEST(VectorExtension, VcpopCountsBitsProducedByVectorComparison)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    writeLanes(vm.registers_, 2, {3, 4, 6, 8});
    writeLanes(vm.registers_, 3, {3, 9, 6, 1});
    vm.registers_.WriteGpr(9, 77);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vmseq.vv v0, v2, v3\n"
        "vcpop.m x9, v0\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadVectorByte(0, 0), 0x05u);
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadGpr(9), 2u);
    vm.Undo();
    EXPECT_EQ(vm.registers_.ReadGpr(9), 77u);
    vm.Redo();
    EXPECT_EQ(vm.registers_.ReadGpr(9), 2u);
}

TEST(VectorExtension, AssemblerEncodesAndDisassemblesMaskedVectorForms)
{
    const auto program = assembleVectorSource(
        "vadd.vv v1, v2, v3, v0.t\n"
        "vredsum.vs v4, v5, v6, v0.t\n"
        "vle8.v v7, (x5), v0.t\n"
        "vse8.v v8, (x6), v0.t\n"
        "vcpop.m x9, v10, v0.t\n");
    ASSERT_EQ(program.text_buffer.size(), 5u);
    for (uint32_t instruction : program.text_buffer)
        EXPECT_EQ((instruction >> 25) & 1u, 0u);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]),
              "vadd.vv v1, v2, v3, v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[1]),
              "vredsum.vs v4, v5, v6, v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[2]),
              "vle8.v v7, (x5), v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[3]),
              "vse8.v v8, (x6), v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[4]),
              "vcpop.m x9, v10, v0.t");
}

TEST(VectorExtension, MaskedArithmeticAndComparisonsPreserveInactiveElements)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    vm.registers_.WriteVectorByte(0, 0, 0x05u); // Activate lanes 0 and 2.
    writeLanes(vm.registers_, 2, {1, 2, 3, 4});
    writeLanes(vm.registers_, 3, {10, 20, 30, 40});
    for (size_t byte = 0; byte < 16; ++byte)
        vm.registers_.WriteVectorByte(1, byte, 0xAAu);
    vm.registers_.WriteVectorByte(4, 0, 0x0Fu);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vadd.vv v1, v2, v3, v0.t\n"
        "vmseq.vv v4, v2, v3, v0.t\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(readElement(vm.registers_, 1, 0, 1), 11u);
    EXPECT_EQ(readElement(vm.registers_, 1, 1, 1), 0xAAu);
    EXPECT_EQ(readElement(vm.registers_, 1, 2, 1), 33u);
    EXPECT_EQ(readElement(vm.registers_, 1, 3, 1), 0xAAu);
    for (size_t byte = 4; byte < 16; ++byte)
        EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), 0xAAu);
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadVectorByte(4, 0), 0x0Au); // Active bits clear; inactive bits remain set.
    // Inactive comparison bits are not modified; lane 1 and lane 3 remain set.
}

TEST(VectorExtension, MaskedReductionAndPopulationCountUseV0Predicate)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 4);
    vm.registers_.WriteVectorByte(0, 0, 0x05u); // Activate lanes 0 and 2.
    vm.registers_.WriteVectorByte(10, 0, 0x0Du); // Source mask has lanes 0, 2, and 3 set.
    writeLanes(vm.registers_, 2, {1, 2, 3, 4});
    writeElement(vm.registers_, 3, 0, 1, 10);
    for (size_t byte = 0; byte < 16; ++byte)
        vm.registers_.WriteVectorByte(4, byte, 0xAAu);
    vm.registers_.WriteGpr(9, 77);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vredsum.vs v4, v2, v3, v0.t\n"
        "vcpop.m x9, v10, v0.t\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(readElement(vm.registers_, 4, 0, 1), 14u);
    EXPECT_EQ(readElement(vm.registers_, 4, 1, 1), 0xAAu);
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadGpr(9), 2u);
}

TEST(VectorExtension, MaskedVectorMemorySkipsInactiveLanesAndSupportsUndoRedo)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    constexpr uint64_t source_address = 0x3000;
    constexpr uint64_t destination_address = 0x4000;
    vm.registers_.WriteGpr(5, 4);
    vm.registers_.WriteGpr(7, source_address);
    vm.registers_.WriteGpr(8, destination_address);
    vm.registers_.WriteVectorByte(0, 0, 0x05u);
    for (size_t byte = 0; byte < 16; ++byte)
    {
        vm.registers_.WriteVectorByte(1, byte, 0xCCu);
        vm.memory_controller_.writeByte(source_address + byte,
                                        static_cast<uint8_t>(0x10u + byte));
        vm.memory_controller_.writeByte(destination_address + byte,
                                        static_cast<uint8_t>(0xA0u + byte));
    }
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vle8.v v1, (x7), v0.t\n"
        "vse8.v v1, (x8), v0.t\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 0), 0x10u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 1), 0xCCu);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 2), 0x12u);
    EXPECT_EQ(vm.registers_.ReadVectorByte(1, 3), 0xCCu);
    vm.Step();
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 0), 0x10u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 1), 0xA1u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 2), 0x12u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 3), 0xA3u);
    vm.Undo();
    for (size_t byte = 0; byte < 16; ++byte)
        EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                  static_cast<uint8_t>(0xA0u + byte));
    vm.Undo();
    for (size_t byte = 0; byte < 16; ++byte)
        EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), 0xCCu);
    vm.Redo();
    vm.Redo();
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 0), 0x10u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 1), 0xA1u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 2), 0x12u);
    EXPECT_EQ(vm.memory_controller_.readByte(destination_address + 3), 0xA3u);
}
TEST(VectorExtension, MaskedStoreDoesNotAccessInactiveOutOfRangeLane)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    const uint64_t last_valid_address = Kites::vm_config::config.getMemorySize() - 1;
    vm.registers_.WriteGpr(5, 2);
    vm.registers_.WriteGpr(8, last_valid_address);
    vm.registers_.WriteVectorByte(0, 0, 0x01u); // Only lane 0 is active.
    vm.registers_.WriteVectorByte(1, 0, 0x5Au);
    vm.registers_.WriteVectorByte(1, 1, 0xA5u);

    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vse8.v v1, (x8), v0.t\n"));
    vm.Step();
    EXPECT_NO_THROW(vm.Step());
    EXPECT_EQ(vm.memory_controller_.readByte(last_valid_address), 0x5Au);

    vm.Undo();
    EXPECT_EQ(vm.memory_controller_.readByte(last_valid_address), 0u);
    vm.Redo();
    EXPECT_EQ(vm.memory_controller_.readByte(last_valid_address), 0x5Au);
}

TEST(VectorExtension, MaskedStorePreflightsAllActiveElementBounds)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    const uint64_t memory_size = Kites::vm_config::config.getMemorySize();
    const uint64_t base_address = memory_size - 8;
    vm.registers_.WriteGpr(5, 2);
    vm.registers_.WriteGpr(8, base_address);
    vm.registers_.WriteVectorByte(0, 0, 0x03u); // Both 64-bit lanes are active.
    for (size_t byte = 0; byte < 16; ++byte)
        vm.registers_.WriteVectorByte(1, byte, static_cast<uint8_t>(byte + 1));

    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 24\n"
        "vse64.v v1, (x8), v0.t\n"));
    vm.Step();
    EXPECT_THROW(vm.Step(), std::out_of_range);

    // Lane 0 is in range, but lane 1 is not. Preflight must prevent a partial store.
    for (size_t byte = 0; byte < 8; ++byte)
        EXPECT_EQ(vm.memory_controller_.readByte(base_address + byte), 0u);
}

TEST(VectorExtension, UnsupportedVtypeIsRejectedByAssembler)
{
    EXPECT_THROW(assembleVectorSource("vsetvli x5, x6, 32\n"), std::runtime_error);
}

TEST(VectorExtension, AssemblerEncodesLogicalAndComparisonInstructions)
{
    const auto program = assembleVectorSource(
        "vand.vv v1, v2, v3\n"
        "vor.vv v4, v5, v6\n"
        "vxor.vv v7, v8, v9\n"
        "vmseq.vv v10, v11, v12\n"
        "vmsne.vv v13, v14, v15\n"
        "vmsltu.vv v16, v17, v18\n"
        "vmslt.vv v19, v20, v21\n");
    ASSERT_EQ(program.text_buffer.size(), 7u);
    const auto encode = [](uint32_t funct6, uint32_t vd, uint32_t vs2, uint32_t vs1)
    {
        return (funct6 << 26) | (1u << 25) | (vs2 << 20) | (vs1 << 15) |
               (vd << 7) | 0x57u;
    };
    EXPECT_EQ(program.text_buffer[0], encode(0b001001, 1, 2, 3));
    EXPECT_EQ(program.text_buffer[1], encode(0b001010, 4, 5, 6));
    EXPECT_EQ(program.text_buffer[2], encode(0b001011, 7, 8, 9));
    EXPECT_EQ(program.text_buffer[3], encode(0b011000, 10, 11, 12));
    EXPECT_EQ(program.text_buffer[4], encode(0b011001, 13, 14, 15));
    EXPECT_EQ(program.text_buffer[5], encode(0b011010, 16, 17, 18));
    EXPECT_EQ(program.text_buffer[6], encode(0b011011, 19, 20, 21));
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]), "vand.vv v1, v2, v3");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[6]), "vmslt.vv v19, v20, v21");
}

TEST(VectorExtension, LogicalOperationsWorkForEverySupportedSew)
{
    const struct Case { int zimm; size_t bytes; } cases[] = {
        {0, 1}, {8, 2}, {16, 4}, {24, 8}
    };
    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.registers_.WriteGpr(5, 2);
        const uint64_t mask =
            test_case.bytes == 8 ? UINT64_MAX : ((uint64_t{1} << (test_case.bytes * 8)) - 1u);
        const uint64_t a = 0xA55AA55AA55AA55AULL & mask;
        const uint64_t b = 0x0FF00FF00FF00FF0ULL & mask;
        writeElement(vm.registers_, 2, 0, test_case.bytes, a);
        writeElement(vm.registers_, 3, 0, test_case.bytes, b);
        writeElement(vm.registers_, 2, 1, test_case.bytes, b);
        writeElement(vm.registers_, 3, 1, test_case.bytes, a);
        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n"
            "vand.vv v4, v2, v3\n"
            "vor.vv v5, v2, v3\n"
            "vxor.vv v6, v2, v3\n"));
        vm.Step(); vm.Step(); vm.Step(); vm.Step();
        EXPECT_EQ(readElement(vm.registers_, 4, 0, test_case.bytes), a & b);
        EXPECT_EQ(readElement(vm.registers_, 5, 0, test_case.bytes), a | b);
        EXPECT_EQ(readElement(vm.registers_, 6, 0, test_case.bytes), a ^ b);
        EXPECT_EQ(readElement(vm.registers_, 4, 1, test_case.bytes), a & b);
        EXPECT_EQ(readElement(vm.registers_, 5, 1, test_case.bytes), a | b);
        EXPECT_EQ(readElement(vm.registers_, 6, 1, test_case.bytes), a ^ b);
    }
}

TEST(VectorExtension, ComparisonsProducePackedMaskBitsForEverySupportedSew)
{
    const struct Case { int zimm; size_t bytes; size_t active_lanes; } cases[] = {
        {0, 1, 4}, {8, 2, 4}, {16, 4, 4}, {24, 8, 2}
    };
    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.registers_.WriteGpr(5, 4);
        const size_t width = test_case.bytes * 8;
        const uint64_t sign_bit = uint64_t{1} << (width - 1);
        writeElement(vm.registers_, 2, 0, test_case.bytes, sign_bit);
        writeElement(vm.registers_, 3, 0, test_case.bytes, 1);
        writeElement(vm.registers_, 2, 1, test_case.bytes, 3);
        writeElement(vm.registers_, 3, 1, test_case.bytes, 3);
        // SEW=64 has VLMAX=2 for VLEN=128, so lanes 2 and 3 do not exist.
        if (test_case.active_lanes > 2)
        {
            writeElement(vm.registers_, 2, 2, test_case.bytes, 2);
            writeElement(vm.registers_, 3, 2, test_case.bytes, 4);
            writeElement(vm.registers_, 2, 3, test_case.bytes, 5);
            writeElement(vm.registers_, 3, 3, test_case.bytes, 4);
        }
        for (size_t reg : {5u, 6u, 7u, 8u})
            vm.registers_.WriteVectorByte(reg, 0, 0xF0u);
        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n"
            "vmseq.vv v5, v2, v3\n"
            "vmsne.vv v6, v2, v3\n"
            "vmslt.vv v7, v2, v3\n"
            "vmsltu.vv v8, v2, v3\n"));
        for (int step = 0; step < 5; ++step) vm.Step();
        const uint8_t active_bits = static_cast<uint8_t>((1u << test_case.active_lanes) - 1u);
        const uint8_t equal_mask = 0x02u;
        const uint8_t not_equal_mask = test_case.active_lanes > 2 ? 0x0Du : 0x01u;
        const uint8_t signed_less_mask = test_case.active_lanes > 2 ? 0x05u : 0x01u;
        const uint8_t unsigned_less_mask = test_case.active_lanes > 2 ? 0x04u : 0x00u;
        EXPECT_EQ(vm.registers_.ReadVectorByte(5, 0), static_cast<uint8_t>(0xF0u | (equal_mask & active_bits)));
        EXPECT_EQ(vm.registers_.ReadVectorByte(6, 0), static_cast<uint8_t>(0xF0u | (not_equal_mask & active_bits)));
        EXPECT_EQ(vm.registers_.ReadVectorByte(7, 0), static_cast<uint8_t>(0xF0u | (signed_less_mask & active_bits)));
        EXPECT_EQ(vm.registers_.ReadVectorByte(8, 0), static_cast<uint8_t>(0xF0u | (unsigned_less_mask & active_bits)));
    }
}


