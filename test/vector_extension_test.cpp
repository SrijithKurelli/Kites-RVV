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

    writeElement(*registers, 1, 0, 4, static_cast<uint32_t>(-1));
    writeElement(*registers, 1, 1, 4, 2);
    writeElement(*registers, 1, 2, 4, static_cast<uint32_t>(-4));
    writeElement(*registers, 1, 3, 4, 4);
    writeElement(*registers, 2, 0, 4, static_cast<uint32_t>(-1));
    writeElement(*registers, 2, 1, 4, 0);
    writeElement(*registers, 2, 2, 4, static_cast<uint32_t>(-4));
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

    EXPECT_EQ(registers->ReadGpr(11), static_cast<uint64_t>(-5));
    EXPECT_EQ(registers->ReadGpr(12), 2u);
    EXPECT_EQ(registers->ReadGpr(13), static_cast<uint64_t>(-2)); // -5 / 2 truncates toward zero.
    EXPECT_EQ(readElement(*registers, 5, 0, 4), 0xFFFFFFFBu);
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

TEST(VectorExtension, ScalarAndVectorSumsCompareDynamicInstructionCounts)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager scalar_manager(nullptr, Kites::ProcessorType::RVSS);
    Kites::ProcessorManager vector_manager(nullptr, Kites::ProcessorType::RVSS);
    scalar_manager.setStepDelay(0);
    vector_manager.setStepDelay(0);

    constexpr uint64_t data_address = 0x1000;
    const uint32_t values[] = {1u, 2u, 3u, 4u};
    for (size_t lane = 0; lane < 4; ++lane)
    {
        scalar_manager.getMemoryController()->writeWord(data_address + lane * 4, values[lane]);
        vector_manager.getMemoryController()->writeWord(data_address + lane * 4, values[lane]);
    }

    scalar_manager.loadProgram(assembleVectorSource(
        "lui x5, 1\n"
        "lw x10, 0(x5)\n"
        "lw x11, 4(x5)\n"
        "lw x12, 8(x5)\n"
        "lw x13, 12(x5)\n"
        "add x14, x10, x11\n"
        "add x14, x14, x12\n"
        "add x14, x14, x13\n"));
    vector_manager.loadProgram(assembleVectorSource(
        "lui x5, 1\n"
        "vsetvli x6, x0, 16\n"
        "vle32.v v1, (x5)\n"
        "vredsum.vs v2, v1, v3\n"));

    scalar_manager.run();
    vector_manager.run();

    EXPECT_EQ(scalar_manager.getRegisterFile()->ReadGpr(14), 10u);
    EXPECT_EQ(readElement(*vector_manager.getRegisterFile(), 2, 0, 4), 10u);
    EXPECT_EQ(scalar_manager.getInstructionsRetired(), 8u);
    EXPECT_EQ(vector_manager.getInstructionsRetired(), 4u);
    EXPECT_LT(vector_manager.getInstructionsRetired(), scalar_manager.getInstructionsRetired());
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

TEST(VectorExtension, AssemblerEncodesAndDisassemblesStridedVectorMemory)
{
    const auto program = assembleVectorSource(
        "vlse8.v v1, (x5), x6\n"
        "vlse16.v v2, (x7), x8, v0.t\n"
        "vlse32.v v3, (x9), x10\n"
        "vlse64.v v4, (x11), x12\n"
        "vsse8.v v13, (x14), x15\n"
        "vsse16.v v16, (x17), x18\n"
        "vsse32.v v19, (x20), x21, v0.t\n"
        "vsse64.v v22, (x23), x24\n");
    ASSERT_EQ(program.text_buffer.size(), 8u);
    for (size_t index = 0; index < program.text_buffer.size(); ++index)
    {
        const uint32_t instruction = program.text_buffer[index];
        EXPECT_EQ(instruction & 0x7Fu, index < 4 ? 0x07u : 0x27u);
        EXPECT_EQ((instruction >> 26) & 0x3u, 0b10u);
    }
    EXPECT_EQ((program.text_buffer[0] >> 20) & 0x1Fu, 6u);
    EXPECT_EQ((program.text_buffer[0] >> 25) & 1u, 1u);
    EXPECT_EQ((program.text_buffer[1] >> 25) & 1u, 0u);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]),
              "vlse8.v v1, (x5), x6");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[1]),
              "vlse16.v v2, (x7), x8, v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[6]),
              "vsse32.v v19, (x20), x21, v0.t");
}

TEST(VectorExtension, StridedVectorMemoryWorksForEverySupportedSew)
{
    struct Case
    {
        int zimm;
        size_t element_bytes;
        const char *load_mnemonic;
        const char *store_mnemonic;
    };
    const Case cases[] = {
        {0, 1, "vlse8.v", "vsse8.v"},
        {8, 2, "vlse16.v", "vsse16.v"},
        {16, 4, "vlse32.v", "vsse32.v"},
        {24, 8, "vlse64.v", "vsse64.v"},
    };

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        constexpr uint64_t source_address = 0x5000;
        constexpr uint64_t destination_address = 0x6000;
        constexpr size_t active_lanes = 2;
        const size_t stride = 2 * test_case.element_bytes;
        vm.registers_.WriteGpr(5, active_lanes);
        vm.registers_.WriteGpr(7, source_address);
        vm.registers_.WriteGpr(8, destination_address);
        vm.registers_.WriteGpr(9, stride);
        for (size_t byte = 0; byte < 16; ++byte)
            vm.registers_.WriteVectorByte(1, byte, 0xCCu);
        for (size_t byte = 0; byte < 6 * test_case.element_bytes; ++byte)
            vm.memory_controller_.writeByte(source_address + byte,
                                            static_cast<uint8_t>(byte + 1));
        for (size_t byte = 0; byte < 32; ++byte)
            vm.memory_controller_.writeByte(destination_address + byte,
                                            static_cast<uint8_t>(0xA0u + byte));

        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n" +
            std::string(test_case.load_mnemonic) + " v1, (x7), x9\n" +
            std::string(test_case.store_mnemonic) + " v1, (x8), x9\n"));
        vm.Step();
        vm.Step();
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
            for (size_t byte = 0; byte < test_case.element_bytes; ++byte)
            {
                const size_t vector_offset = lane * test_case.element_bytes + byte;
                const size_t memory_offset = lane * stride + byte;
                EXPECT_EQ(vm.registers_.ReadVectorByte(1, vector_offset),
                          static_cast<uint8_t>(memory_offset + 1));
            }
        }
        for (size_t byte = active_lanes * test_case.element_bytes; byte < 16; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), 0xCCu);

        vm.Step();
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
            for (size_t byte = 0; byte < test_case.element_bytes; ++byte)
            {
                const size_t memory_offset = lane * stride + byte;
                EXPECT_EQ(vm.memory_controller_.readByte(destination_address + memory_offset),
                          static_cast<uint8_t>(memory_offset + 1));
            }
        }
        for (size_t byte = 0; byte < 32; ++byte)
        {
            const bool written = byte < test_case.element_bytes ||
                (byte >= stride && byte < stride + test_case.element_bytes);
            if (!written)
                EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                          static_cast<uint8_t>(0xA0u + byte));
        }

        vm.Undo();
        for (size_t byte = 0; byte < 32; ++byte)
            EXPECT_EQ(vm.memory_controller_.readByte(destination_address + byte),
                      static_cast<uint8_t>(0xA0u + byte));
        vm.Undo();
        for (size_t byte = 0; byte < 16; ++byte)
            EXPECT_EQ(vm.registers_.ReadVectorByte(1, byte), 0xCCu);
        vm.Redo();
        vm.Redo();
        for (size_t lane = 0; lane < active_lanes; ++lane)
            EXPECT_EQ(vm.memory_controller_.readByte(
                          destination_address + lane * stride),
                      static_cast<uint8_t>(lane * stride + 1));
    }
}

TEST(VectorExtension, NegativeStridedMemoryAndMaskingWork)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    constexpr uint64_t source_address = 0x7000;
    constexpr uint64_t destination_address = 0x7100;
    vm.registers_.WriteGpr(5, 3);
    vm.registers_.WriteGpr(7, source_address + 16);
    vm.registers_.WriteGpr(8, destination_address + 16);
    vm.registers_.WriteGpr(9, static_cast<uint64_t>(-8));
    vm.registers_.WriteVectorByte(0, 0, 0x05u); // Enable lanes 0 and 2 only.
    for (size_t word = 0; word < 5; ++word)
        vm.memory_controller_.writeWord(source_address + word * 4,
                                        static_cast<uint32_t>(10 + word * 10));
    for (size_t byte = 0; byte < 24; ++byte)
        vm.memory_controller_.writeByte(destination_address + byte, 0xA5u);

    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 16\n"
        "vlse32.v v1, (x7), x9, v0.t\n"
        "vsse32.v v1, (x8), x9, v0.t\n"));
    vm.Step();
    vm.Step();
    EXPECT_EQ(readElement(vm.registers_, 1, 0, 4), 50u);
    EXPECT_EQ(readElement(vm.registers_, 1, 1, 4), 0u); // Masked-off lane is undisturbed.
    EXPECT_EQ(readElement(vm.registers_, 1, 2, 4), 10u);
    vm.Step();
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address + 16), 50u);
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address + 8), 0xA5A5A5A5u);
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address), 10u);
    vm.Undo();
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address + 16), 0xA5A5A5A5u);
    vm.Undo();
    EXPECT_EQ(readElement(vm.registers_, 1, 0, 4), 0u);
    vm.Redo();
    vm.Redo();
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address + 16), 50u);
    EXPECT_EQ(vm.memory_controller_.readWord(destination_address), 10u);
}

TEST(VectorExtension, ZeroStrideLoadsRepeatTheSameElement)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    constexpr uint64_t source_address = 0x8000;
    vm.registers_.WriteGpr(5, 3);
    vm.registers_.WriteGpr(7, source_address);
    vm.registers_.WriteGpr(9, 0);
    vm.memory_controller_.writeWord(source_address, 0x12345678u);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 16\n"
        "vlse32.v v1, (x7), x9\n"));
    vm.Step();
    vm.Step();
    for (size_t lane = 0; lane < 3; ++lane)
        EXPECT_EQ(readElement(vm.registers_, 1, lane, 4), 0x12345678u);
}

TEST(VectorExtension, StridedStorePreflightsEveryActiveLaneAddress)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 2);
    constexpr uint64_t valid_address = 0x1000;
    vm.registers_.WriteGpr(7, valid_address);
    vm.registers_.WriteGpr(8, static_cast<uint64_t>(-0x2000));
    vm.registers_.WriteVectorByte(1, 0, 0x5Au);
    vm.registers_.WriteVectorByte(1, 1, 0xA5u);
    vm.memory_controller_.writeByte(valid_address, 0xD7u);
    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vsse8.v v1, (x7), x8\n"));
    vm.Step();
    EXPECT_THROW(vm.Step(), std::out_of_range);
    EXPECT_EQ(vm.memory_controller_.readByte(valid_address), 0xD7u);
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



TEST(VectorExtension, AssemblerEncodesAndDisassemblesSelectedBitManipulationInstructions)
{
    const auto program = assembleVectorSource(
        "vandn.vv v1, v2, v3\n"
        "vrol.vv v4, v5, v6\n"
        "vror.vv v7, v8, v9\n"
        "vclz.v v10, v11\n"
        "vctz.v v12, v13, v0.t\n"
        "vcpop.v v14, v15\n");
    ASSERT_EQ(program.text_buffer.size(), 6u);

    const auto opivv = [](uint32_t funct6, uint32_t vd, uint32_t vs2, uint32_t vs1,
                          uint32_t vm = 1u)
    {
        return (funct6 << 26) | (vm << 25) | (vs2 << 20) | (vs1 << 15) |
               (vd << 7) | 0x57u;
    };
    const auto opmvv_unary = [](uint32_t unary_vs1, uint32_t vd, uint32_t vs2,
                                uint32_t vm = 1u)
    {
        return (0b010010u << 26) | (vm << 25) | (vs2 << 20) |
               (unary_vs1 << 15) | (0b010u << 12) | (vd << 7) | 0x57u;
    };

    EXPECT_EQ(program.text_buffer[0], opivv(0b000001, 1, 2, 3));
    EXPECT_EQ(program.text_buffer[1], opivv(0b010101, 4, 5, 6));
    EXPECT_EQ(program.text_buffer[2], opivv(0b010100, 7, 8, 9));
    EXPECT_EQ(program.text_buffer[3], opmvv_unary(0b01100, 10, 11));
    EXPECT_EQ(program.text_buffer[4], opmvv_unary(0b01101, 12, 13, 0));
    EXPECT_EQ(program.text_buffer[5], opmvv_unary(0b01110, 14, 15));

    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]),
              "vandn.vv v1, v2, v3");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[1]),
              "vrol.vv v4, v5, v6");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[2]),
              "vror.vv v7, v8, v9");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[3]),
              "vclz.v v10, v11");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[4]),
              "vctz.v v12, v13, v0.t");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[5]),
              "vcpop.v v14, v15");
}

TEST(VectorExtension, PerElementBitCountsWorkForEverySupportedSew)
{
    const struct Case { int zimm; size_t sew; size_t bytes; size_t lanes; } cases[] = {
        {0, 8, 1, 16}, {8, 16, 2, 8}, {16, 32, 4, 4}, {24, 64, 8, 2},
    };

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        const uint64_t element_mask = test_case.sew == 64
                                          ? UINT64_MAX
                                          : ((uint64_t{1} << test_case.sew) - 1u);
        writeElement(vm.registers_, 2, 0, test_case.bytes, 0);
        writeElement(vm.registers_, 2, 1, test_case.bytes,
                     uint64_t{1} << (test_case.sew - 1));
        if (test_case.lanes > 2)
            writeElement(vm.registers_, 2, 2, test_case.bytes, 1);
        if (test_case.lanes > 3)
            writeElement(vm.registers_, 2, 3, test_case.bytes, element_mask);
        for (size_t byte = 0; byte < 16; ++byte)
        {
            vm.registers_.WriteVectorByte(3, byte, 0xAAu);
            vm.registers_.WriteVectorByte(4, byte, 0xAAu);
            vm.registers_.WriteVectorByte(5, byte, 0xAAu);
        }

        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x0, " + std::to_string(test_case.zimm) + "\n"
            "vclz.v v3, v2\n"
            "vctz.v v4, v2\n"
            "vcpop.v v5, v2\n"));
        for (int step = 0; step < 4; ++step)
            vm.Step();

        EXPECT_EQ(readElement(vm.registers_, 3, 0, test_case.bytes), test_case.sew);
        EXPECT_EQ(readElement(vm.registers_, 4, 0, test_case.bytes), test_case.sew);
        EXPECT_EQ(readElement(vm.registers_, 5, 0, test_case.bytes), 0u);
        EXPECT_EQ(readElement(vm.registers_, 3, 1, test_case.bytes), 0u);
        EXPECT_EQ(readElement(vm.registers_, 4, 1, test_case.bytes), test_case.sew - 1u);
        EXPECT_EQ(readElement(vm.registers_, 5, 1, test_case.bytes), 1u);
        if (test_case.lanes > 2)
        {
            EXPECT_EQ(readElement(vm.registers_, 3, 2, test_case.bytes), test_case.sew - 1u);
            EXPECT_EQ(readElement(vm.registers_, 4, 2, test_case.bytes), 0u);
            EXPECT_EQ(readElement(vm.registers_, 5, 2, test_case.bytes), 1u);
        }
        if (test_case.lanes > 3)
        {
            EXPECT_EQ(readElement(vm.registers_, 3, 3, test_case.bytes), 0u);
            EXPECT_EQ(readElement(vm.registers_, 4, 3, test_case.bytes), 0u);
            EXPECT_EQ(readElement(vm.registers_, 5, 3, test_case.bytes), test_case.sew);
        }
    }
}

TEST(VectorExtension, AndNotAndRotatesWorkForEverySupportedSew)
{
    const struct Case { int zimm; size_t sew; size_t bytes; } cases[] = {
        {0, 8, 1}, {8, 16, 2}, {16, 32, 4}, {24, 64, 8},
    };

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::RVSSProcessor vm;
        vm.registers_.WriteGpr(5, 2);
        const uint64_t element_mask = test_case.sew == 64
                                          ? UINT64_MAX
                                          : ((uint64_t{1} << test_case.sew) - 1u);
        const uint64_t data0 = 0xA55AA55AA55AA55AULL & element_mask;
        const uint64_t data1 = 0x123456789ABCDEF0ULL & element_mask;
        const uint64_t operand0 = 0x0FF00FF00FF00FF0ULL & element_mask;
        const uint64_t operand1 = 0xFEDCBA9876543210ULL & element_mask;
        writeElement(vm.registers_, 2, 0, test_case.bytes, data0);
        writeElement(vm.registers_, 2, 1, test_case.bytes, data1);
        writeElement(vm.registers_, 3, 0, test_case.bytes, operand0);
        writeElement(vm.registers_, 3, 1, test_case.bytes, operand1);
        writeElement(vm.registers_, 4, 0, test_case.bytes, 1);
        writeElement(vm.registers_, 4, 1, test_case.bytes, test_case.sew + 1u);

        vm.LoadProgram(assembleVectorSource(
            "vsetvli x6, x5, " + std::to_string(test_case.zimm) + "\n"
            "vandn.vv v5, v2, v3\n"
            "vrol.vv v6, v2, v4\n"
            "vror.vv v7, v2, v4\n"));
        for (int step = 0; step < 4; ++step)
            vm.Step();

        const auto rotate_left = [test_case](uint64_t value, size_t shift)
        {
            const uint64_t mask = test_case.sew == 64
                                      ? UINT64_MAX
                                      : ((uint64_t{1} << test_case.sew) - 1u);
            shift &= test_case.sew - 1u;
            return shift == 0
                       ? value & mask
                       : ((value << shift) | (value >> (test_case.sew - shift))) & mask;
        };
        const auto rotate_right = [test_case](uint64_t value, size_t shift)
        {
            const uint64_t mask = test_case.sew == 64
                                      ? UINT64_MAX
                                      : ((uint64_t{1} << test_case.sew) - 1u);
            shift &= test_case.sew - 1u;
            return shift == 0
                       ? value & mask
                       : ((value >> shift) | (value << (test_case.sew - shift))) & mask;
        };

        EXPECT_EQ(readElement(vm.registers_, 5, 0, test_case.bytes),
                  data0 & (~operand0) & element_mask);
        EXPECT_EQ(readElement(vm.registers_, 5, 1, test_case.bytes),
                  data1 & (~operand1) & element_mask);
        EXPECT_EQ(readElement(vm.registers_, 6, 0, test_case.bytes), rotate_left(data0, 1));
        EXPECT_EQ(readElement(vm.registers_, 6, 1, test_case.bytes),
                  rotate_left(data1, test_case.sew + 1u));
        EXPECT_EQ(readElement(vm.registers_, 7, 0, test_case.bytes), rotate_right(data0, 1));
        EXPECT_EQ(readElement(vm.registers_, 7, 1, test_case.bytes),
                  rotate_right(data1, test_case.sew + 1u));
    }
}

TEST(VectorExtension, MaskedBitCountsPreserveInactiveAndTailElements)
{
    Kites::setupVmStateDirectory();
    Kites::RVSSProcessor vm;
    vm.registers_.WriteGpr(5, 3);
    vm.registers_.WriteVectorByte(0, 0, 0x05u);
    writeElement(vm.registers_, 2, 0, 1, 0);
    writeElement(vm.registers_, 2, 1, 1, 0x80);
    writeElement(vm.registers_, 2, 2, 1, 0xFF);
    for (size_t byte = 0; byte < 16; ++byte)
        vm.registers_.WriteVectorByte(3, byte, 0xAAu);

    vm.LoadProgram(assembleVectorSource(
        "vsetvli x6, x5, 0\n"
        "vclz.v v3, v2, v0.t\n"
        "nop\n"));
    vm.Step();
    vm.Step();
    vm.Step();

    EXPECT_EQ(readElement(vm.registers_, 3, 0, 1), 8u);
    EXPECT_EQ(readElement(vm.registers_, 3, 1, 1), 0xAAu);
    EXPECT_EQ(readElement(vm.registers_, 3, 2, 1), 0u);
    for (size_t byte = 3; byte < 16; ++byte)
        EXPECT_EQ(vm.registers_.ReadVectorByte(3, byte), 0xAAu);
}

TEST(VectorExtension, AssemblerEncodesAndDisassemblesVectorMultiply)
{
    const auto program = assembleVectorSource(
        "vmul.vv v1, v2, v3\n"
        "vmul.vv v4, v5, v6, v0.t\n"
        "vmul.vx v7, v8, x9\n"
        "vmul.vx v10, v11, a0, v0.t\n"
        "vadd.vx v12, v13, x14\n"
        "vadd.vi v15, v16, -3\n"
        "vadd.vi v17, v18, 15, v0.t\n");

    ASSERT_EQ(program.text_buffer.size(), 7u);
    const uint32_t vmul_vv =
        (0b100101u << 26) | (1u << 25) | (2u << 20) | (3u << 15) | (1u << 7) | 0x57u;
    const uint32_t masked_vmul_vv =
        (0b100101u << 26) | (5u << 20) | (6u << 15) | (4u << 7) | 0x57u;
    const uint32_t vmul_vx =
        (0b100101u << 26) | (1u << 25) | (8u << 20) | (9u << 15) |
        (0b100u << 12) | (7u << 7) | 0x57u;
    const uint32_t masked_vmul_vx =
        (0b100101u << 26) | (11u << 20) | (10u << 15) |
        (0b100u << 12) | (10u << 7) | 0x57u;

    EXPECT_EQ(program.text_buffer[0], vmul_vv);
    EXPECT_EQ(program.text_buffer[1], masked_vmul_vv);
    EXPECT_EQ(program.text_buffer[2], vmul_vx);
    EXPECT_EQ(program.text_buffer[3], masked_vmul_vx);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]),
              "vmul.vv v1, v2, v3");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[2]),
              "vmul.vx v7, v8, x9");
    EXPECT_EQ(program.text_buffer[4],
              (1u << 25) | (13u << 20) | (14u << 15) | (0b100u << 12) |
              (12u << 7) | 0x57u);
    EXPECT_EQ(program.text_buffer[5],
              (16u << 20) | (29u << 15) | (0b011u << 12) | (15u << 7) | 0x57u);
    EXPECT_EQ(program.text_buffer[6],
              (18u << 20) | (15u << 15) | (0b011u << 12) | (17u << 7) | 0x57u);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[4]),
              "vadd.vx v12, v13, x14");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[5]),
              "vadd.vi v15, v16, -3");
}

TEST(VectorExtension, VectorMultiplyExecutesForEverySupportedSEW)
{
    struct Case
    {
        int zimm;
        size_t element_bytes;
    };
    const Case cases[] = {{0, 1}, {8, 2}, {16, 4}, {24, 8}};

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
        auto *registers = manager.getRegisterFile();
        const size_t lanes = 16 / test_case.element_bytes;
        const uint64_t element_mask = test_case.element_bytes == 8
            ? UINT64_MAX
            : ((uint64_t{1} << (test_case.element_bytes * 8)) - 1u);
        constexpr uint64_t scalar = 7;

        registers->WriteGpr(5, lanes);
        registers->WriteGpr(6, scalar);
        for (size_t lane = 0; lane < lanes; ++lane)
        {
            writeElement(*registers, 1, lane, test_case.element_bytes, lane + 2);
            writeElement(*registers, 2, lane, test_case.element_bytes, lane + 3);
        }

        const std::string source =
            "vsetvli x7, x5, " + std::to_string(test_case.zimm) + "\n" +
            "vmul.vv v3, v1, v2\n"
            "vmul.vx v4, v1, x6\n"
            "vadd.vx v5, v1, x6\n"
            "vadd.vi v6, v1, -2\n";
        manager.loadProgram(assembleVectorSource(source));
        manager.step();
        manager.step();
        manager.step();
        manager.step();
        manager.step();

        for (size_t lane = 0; lane < lanes; ++lane)
        {
            const uint64_t a = lane + 2;
            const uint64_t b = lane + 3;
            EXPECT_EQ(readElement(*registers, 3, lane, test_case.element_bytes),
                      (a * b) & element_mask);
            EXPECT_EQ(readElement(*registers, 4, lane, test_case.element_bytes),
                      (a * scalar) & element_mask);
            EXPECT_EQ(readElement(*registers, 5, lane, test_case.element_bytes),
                      (a + scalar) & element_mask);
            EXPECT_EQ(readElement(*registers, 6, lane, test_case.element_bytes),
                      (a - 2u) & element_mask);
        }
    }
}

TEST(VectorExtension, MaskedVectorMultiplyPreservesInactiveLanes)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    auto *registers = manager.getRegisterFile();

    writeElement(*registers, 0, 0, 1, 0b0101);
    for (size_t lane = 0; lane < 4; ++lane)
    {
        writeElement(*registers, 1, lane, 4, lane + 2);
        writeElement(*registers, 2, lane, 4, 10);
        writeElement(*registers, 3, lane, 4, 99);
    }

    manager.getRegisterFile()->WriteGpr(5, 4);
    manager.loadProgram(assembleVectorSource(
        "vsetvli x6, x5, 16\n"
        "vmul.vv v3, v1, v2, v0.t\n"));
    manager.step();
    manager.step();

    EXPECT_EQ(readElement(*registers, 3, 0, 4), 20u);
    EXPECT_EQ(readElement(*registers, 3, 1, 4), 99u);
    EXPECT_EQ(readElement(*registers, 3, 2, 4), 40u);
    EXPECT_EQ(readElement(*registers, 3, 3, 4), 99u);
}

TEST(VectorExtension, DotProductComposesVectorMultiplyAndReduction)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    auto *registers = manager.getRegisterFile();

    const uint32_t lhs[] = {1, 2, 3, 4};
    const uint32_t rhs[] = {5, 6, 7, 8};
    for (size_t lane = 0; lane < 4; ++lane)
    {
        writeElement(*registers, 1, lane, 4, lhs[lane]);
        writeElement(*registers, 2, lane, 4, rhs[lane]);
    }

    manager.getRegisterFile()->WriteGpr(5, 4);
    manager.loadProgram(assembleVectorSource(
        "vsetvli x6, x5, 16\n"
        "vmul.vv v3, v1, v2\n"
        "vredsum.vs v4, v3, v0\n"
        "nop\n"));
    for (int instruction = 0; instruction < 4; ++instruction)
        manager.step();

    EXPECT_EQ(readElement(*registers, 3, 0, 4), 5u);
    EXPECT_EQ(readElement(*registers, 3, 1, 4), 12u);
    EXPECT_EQ(readElement(*registers, 3, 2, 4), 21u);
    EXPECT_EQ(readElement(*registers, 3, 3, 4), 32u);
    EXPECT_EQ(readElement(*registers, 4, 0, 4), 70u);
}

TEST(VectorExtension, AssemblerEncodesAndDisassemblesScalarVectorMoves)
{
    const auto program = assembleVectorSource(
        "vmv.x.s x10, v1\n"
        "vmv.s.x v2, a1\n");
    ASSERT_EQ(program.text_buffer.size(), 2u);

    const uint32_t vmv_x_s =
        (0b010000u << 26) | (1u << 25) | (1u << 20) |
        (0b010u << 12) | (10u << 7) | 0x57u;
    const uint32_t vmv_s_x =
        (0b010000u << 26) | (1u << 25) | (11u << 15) |
        (0b110u << 12) | (2u << 7) | 0x57u;

    EXPECT_EQ(program.text_buffer[0], vmv_x_s);
    EXPECT_EQ(program.text_buffer[1], vmv_s_x);
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[0]), "vmv.x.s x10, v1");
    EXPECT_EQ(Kites::instruction_set::disassemble(program.text_buffer[1]), "vmv.s.x v2, x11");
}

TEST(VectorExtension, ScalarVectorMovesRespectSEWAndSignExtension)
{
    struct Case
    {
        int zimm;
        size_t element_bytes;
        uint64_t vector_value;
    };
    const Case cases[] = {
        {0, 1, 0x80u},
        {8, 2, 0x8001u},
        {16, 4, 0x80000001u},
        {24, 8, 0x8000000000000001ull}
    };
    constexpr uint64_t scalar = 0x1122334455667788ull;

    for (const auto &test_case : cases)
    {
        Kites::setupVmStateDirectory();
        Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
        auto *registers = manager.getRegisterFile();
        registers->WriteGpr(5, 1);
        registers->WriteGpr(6, scalar);
        writeElement(*registers, 1, 0, test_case.element_bytes, test_case.vector_value);
        for (size_t byte = test_case.element_bytes; byte < 16; ++byte)
            registers->WriteVectorByte(1, byte, 0xA5u);
        for (size_t byte = 0; byte < 16; ++byte)
            registers->WriteVectorByte(2, byte, 0x5Au);

        const std::string source =
            "vsetvli x8, x5, " + std::to_string(test_case.zimm) + "\n" +
            "vmv.x.s x7, v1\n"
            "vmv.s.x v3, x6\n";
        manager.loadProgram(assembleVectorSource(source));
        manager.step();
        manager.step();
        manager.step();

        uint64_t expected_read = test_case.vector_value;
        const size_t width_bits = test_case.element_bytes * 8;
        if (width_bits < 64 && ((expected_read >> (width_bits - 1)) & 1u))
            expected_read |= (~uint64_t{0}) << width_bits;
        EXPECT_EQ(registers->ReadGpr(7), expected_read);

        const uint64_t element_mask = width_bits == 64
            ? UINT64_MAX
            : ((uint64_t{1} << width_bits) - 1u);
        EXPECT_EQ(readElement(*registers, 3, 0, test_case.element_bytes), scalar & element_mask);
        for (size_t byte = test_case.element_bytes; byte < 16; ++byte)
            EXPECT_EQ(registers->ReadVectorByte(3, byte), 0x5Au);
    }
}

TEST(VectorExtension, ScalarVectorMoveSemanticsWhenVectorLengthIsZero)
{
    Kites::setupVmStateDirectory();
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    auto *registers = manager.getRegisterFile();
    writeElement(*registers, 1, 0, 4, 0xFFFFFFFFu);
    registers->WriteVectorByte(2, 0, 0x34u);
    registers->WriteGpr(6, 0x12345678u);
    registers->WriteGpr(7, 4);

    manager.loadProgram(assembleVectorSource(
        "vsetvli x0, x0, 16\n"
        "vmv.x.s x8, v1\n"
        "vmv.s.x v2, x6\n"));
    manager.step();
    manager.step();
    manager.step();

    EXPECT_EQ(registers->ReadGpr(8), static_cast<uint64_t>(-1));
    EXPECT_EQ(registers->ReadVectorByte(2, 0), 0x34u);
}
