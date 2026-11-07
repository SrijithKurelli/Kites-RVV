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

TEST(VectorExtension, UnsupportedVtypeIsRejectedByAssembler)
{
    EXPECT_THROW(assembleVectorSource("vsetvli x5, x6, 32\n"), std::runtime_error);
}

