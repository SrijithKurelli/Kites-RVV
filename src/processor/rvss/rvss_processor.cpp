/**
 * @file rvss_vm.cpp
 * @brief RVSS VM implementation
 * @author Vishank Singh, https://github.com/VishankSingh
 */

#include "processor/rvss/rvss_processor.h"

#include "common/instructions.h"
#include "config/config.h"
#include "common/globals.h"
#include "ui/processor_tab/processor_designs/rvss_processor_circuit_scene.h"
#include "utils/utils.h"
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <stack>
#include <thread>
#include <tuple>

namespace Kites
{
namespace
{
bool isVectorMemoryInstruction(uint32_t instruction)
{
    const uint8_t opcode = instruction & 0x7Fu;
    const uint8_t width = (instruction >> 12) & 0x7u;
    return (opcode == 0x07u || opcode == 0x27u) &&
           (width == 0b000u || width == 0b101u || width == 0b110u || width == 0b111u);
}

size_t vectorMemoryElementBytes(uint32_t instruction)
{
    switch ((instruction >> 12) & 0x7u)
    {
    case 0b000: return 1;
    case 0b101: return 2;
    case 0b110: return 4;
    case 0b111: return 8;
    default: throw std::runtime_error("Unsupported vector memory element width");
    }
}

uint64_t readVectorMemoryElement(MemoryController &memory, uint64_t address, size_t bytes)
{
    switch (bytes)
    {
    case 1: return memory.readByte(address);
    case 2: return memory.readHalfWord(address);
    case 4: return memory.readWord(address);
    case 8: return memory.readDoubleWord(address);
    default: throw std::runtime_error("Unsupported vector memory element width");
    }
}

void writeVectorMemoryElement(MemoryController &memory, uint64_t address,
                              size_t bytes, uint64_t value)
{
    switch (bytes)
    {
    case 1: memory.writeByte(address, static_cast<uint8_t>(value)); break;
    case 2: memory.writeHalfWord(address, static_cast<uint16_t>(value)); break;
    case 4: memory.writeWord(address, static_cast<uint32_t>(value)); break;
    case 8: memory.writeDoubleWord(address, value); break;
    default: throw std::runtime_error("Unsupported vector memory element width");
    }
}
} // namespace

RVSSProcessor::RVSSProcessor() : ProcessorBase()
{
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Kites::RVSSCircuitScene>();
    connect(this, &ProcessorBase::updateCircuitStateSignal, circuit_scene_.get(),
            &Kites::RVSSCircuitScene::updateCircuitState, Qt::QueuedConnection);
#endif

    active_wires_.append("IM_to_PC_pc");
    active_wires_.append("PC_to_IM_instruction");
    active_wires_.append("IM_to_RF_rs1_whole");
    active_wires_.append("RF_to_ALUMUX");
    active_wires_.append("PCMux_to_PC");
    active_wires_.append("Imm_to_ALUMUX");
    active_wires_.append("BranchALU_to_PCMUX");
    active_wires_.append("PCALU_to_PCMUX");
    active_wires_.append("Control_ALUOp");
    active_wires_.append("PC_to_InstrMem");
    active_wires_.append("ALUOp_to_ALU_alusignal");
    active_wires_.append("ALU_to_DM");

    always_active_wires_count_ = active_wires_.size();
}

RVSSProcessor::~RVSSProcessor() = default;

void RVSSProcessor::SetActiveWireNames()
{
    // first clear the current list
    // TODO * we dont have to clear wire that will always be active
    //  so make a list of those wires and only clear the rest
    //  for now this will
    // active_wires_.clear();

    // Map control signals to canonical wire names used in the scene JSON.
    // maybe sending integers would be better?
    // but for now, this is fine.
    // also the preformace boost well get of that will that really improve the user experience that
    // much?

    // first add the always active wires
    // this is not ideal but for now it will do

    // if(branch_flag_)
    // {

    // }

    if (control_unit_.GetMemWrite())
    {
        active_wires_.append("Control_to_MemWrite");
    }
    if (control_unit_.GetMemRead())
    {
        active_wires_.append("Control_to_MemRead");
    }
    if (control_unit_.GetRegWrite())
    {
        active_wires_.append("Control_to_Regwrite");
        active_wires_.append("WBMux_to_RF_lowest");
    }
    if (control_unit_.GetBranch())
    {
        active_wires_.append("Control_to_BranchAND");
    }
    if (control_unit_.GetAluSrc())
    {
        active_wires_.append("Control_to_ALUSrc_control");
    }
    if (control_unit_.GetMemToReg())
    {
        active_wires_.append("Control_to_MemtoRegMux");
    }

}

void RVSSProcessor::setProcessorState()
{
    processor_state_.programCounters = 
    {program_counter_,INVALID_PC,INVALID_PC,INVALID_PC,INVALID_PC};
    processor_state_.lastExecutedPC = last_executed_pc_;
}

void RVSSProcessor::Run()
{
    ClearStop();
    while (!stop_requested_ && program_counter_ < program_size_)
    {
        if (last_breakpoint_pc_ != UINT64_MAX && program_counter_ != last_breakpoint_pc_)
        {
            last_breakpoint_pc_ = UINT64_MAX;
        }

        if (std::find(breakpoints_.begin(), breakpoints_.end(), program_counter_) !=
                breakpoints_.end() &&
            program_counter_ != last_breakpoint_pc_)
        {
            pause_requested_ = true;
            last_breakpoint_pc_ = program_counter_;
            emit processorPausedAtBreakpointSignal();
        }
        {
            QMutexLocker locker(&pause_mutex_);
            while (pause_requested_ && !stop_requested_)
            {
                pause_wait_condition_.wait(&pause_mutex_);
            }
            if (stop_requested_)
            {
                break;
            }
        }
        StepAndNotify();
        // handling the delay
        {
            QMutexLocker locker(&pause_mutex_);
            if (stop_requested_ || pause_requested_)
                continue;
            pause_wait_condition_.wait(&pause_mutex_, step_delay_);
            // we wait for step_delay_ milliseconds or until notified to wake up
        }
    }
    if (stop_requested_)
    {
        emit processorClockedSignal(processor_state_);
    }
    if (!stop_requested_ && program_counter_ >= program_size_)
    {
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

void RVSSProcessor::Fetch()
{
    current_instruction_ = memory_controller_.readInstruction(program_counter_);
    UpdateProgramCounter(4);
}

void RVSSProcessor::Decode()
{
    control_unit_.SetControlSignals(current_instruction_);
}

void RVSSProcessor::Execute()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        HandleSyscall();
        return;
    }

    // Opcode 0x57 is the RISC-V Vector extension opcode. It must be dispatched
    // before scalar operand reads because rd/rs1/rs2 name vector registers here.
    if (opcode == 0b1010111)
    {
        ExecuteVector();
        return;
    }

    // Unit-stride vector memory instructions use rs1 as a scalar base address.
    // Their vector register fields must not pass through scalar GPR decoding.
    if (isVectorMemoryInstruction(current_instruction_))
    {
        const uint8_t base_register = (current_instruction_ >> 15) & 0x1Fu;
        execution_result_ = registers_.ReadGpr(base_register);
        return;
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        ExecuteFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        ExecuteDouble();
        return;
    }
    else if (opcode == 0b1110011)
    {
        ExecuteCsr();
        return;
    }

    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    int32_t imm = ImmGenerator(current_instruction_);

    uint64_t reg1_value = registers_.ReadGpr(rs1);
    uint64_t reg2_value = registers_.ReadGpr(rs2);

    bool overflow = false;

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, overflow) = alu_.execute(aluOperation, reg1_value, reg2_value);

    if (control_unit_.GetBranch())
    {
        if (opcode == 0b1100111 || opcode == 0b1101111)
        {                                                      // JALR or JAL
            next_pc_ = static_cast<int64_t>(program_counter_); // PC was already updated in Fetch()
            UpdateProgramCounter(-4);
            return_address_ = program_counter_ + 4;
            if (opcode == 0b1100111)
            { // JALR: target = (rs1 + imm) & ~1
                UpdateProgramCounter(-program_counter_ + (execution_result_ & ~1ULL));
            }
            else
            { // JAL
                UpdateProgramCounter(imm);
            }
        }
        else if (opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbeq)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbne)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kblt)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbge)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbltu)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbgeu)
                               .opcode)
        {
            switch (funct3)
            {
            case 0b000:
            { // BEQ
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            case 0b001:
            { // BNE
                branch_flag_ = (execution_result_ != 0);
                break;
            }
            case 0b100:
            { // BLT
                branch_flag_ = (execution_result_ == 1);
                break;
            }
            case 0b101:
            { // BGE
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            case 0b110:
            { // BLTU
                branch_flag_ = (execution_result_ == 1);
                break;
            }
            case 0b111:
            { // BGEU
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            }
        }
    }

    if (branch_flag_ &&
        (opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbeq)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbne)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kblt)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbge)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbltu)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbgeu)
                       .opcode))
    {
        UpdateProgramCounter(-4);
        UpdateProgramCounter(imm);
        // we have jumped so the branch alu wire will send signal to the pc mux
        active_wires_.append("ALUzero_to_ANDGATElower");
        active_wires_.append("BranchAND_to_PCMUX");
    }

    if (opcode == 0b0010111)
    { // AUIPC
        execution_result_ = static_cast<int64_t>(program_counter_) - 4 + (imm << 12);
    }
}

void RVSSProcessor::ExecuteFloat()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rm = funct3;
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t rs3 = (current_instruction_ >> 27) & 0b11111;

    uint8_t fcsr_status = 0;

    int32_t imm = ImmGenerator(current_instruction_);

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = registers_.ReadFpr(rs1);
    uint64_t reg2_value = registers_.ReadFpr(rs2);
    uint64_t reg3_value = registers_.ReadFpr(rs3);

    if (funct7 == 0b1101000 || funct7 == 0b1111000 || opcode == 0b0000111 || opcode == 0b0100111)
    {
        reg1_value = registers_.ReadGpr(rs1);
    }

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    registers_.WriteCsr(0x003, fcsr_status);
}

void RVSSProcessor::ExecuteDouble()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rm = funct3;
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t rs3 = (current_instruction_ >> 27) & 0b11111;

    uint8_t fcsr_status = 0;

    int32_t imm = ImmGenerator(current_instruction_);

    if (rm == 0b111) // dynamic rounding mode: use frm, like ExecuteFloat
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = registers_.ReadFpr(rs1);
    uint64_t reg2_value = registers_.ReadFpr(rs2);
    uint64_t reg3_value = registers_.ReadFpr(rs3);

    if (funct7 == 0b1101001 || funct7 == 0b1111001 || opcode == 0b0000111 || opcode == 0b0100111)
    {
        reg1_value = registers_.ReadGpr(rs1);
    }

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, fcsr_status) =
        alu::Alu::dfpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);
}

void RVSSProcessor::ExecuteVector()
{
    constexpr uint8_t kVectorOpcode = 0b1010111;
    const uint8_t funct3 = (current_instruction_ >> 12) & 0x7;
    const uint8_t vd = (current_instruction_ >> 7) & 0x1F;
    const uint8_t rs1 = (current_instruction_ >> 15) & 0x1F;

    if ((current_instruction_ & 0x7F) != kVectorOpcode)
        throw std::runtime_error("Invalid vector opcode");

    // vsetvli rd, rs1, zimm: configure SEW/LMUL and derive vl from the AVL rules.
    if (funct3 == 0b111)
    {
        if ((current_instruction_ & 0x80000000u) != 0)
            throw std::runtime_error("vsetivli and vsetvl are not supported; use vsetvli");
        const uint16_t zimm = static_cast<uint16_t>((current_instruction_ >> 20) & 0x7FFu);
        if (zimm != 0 && zimm != 8 && zimm != 16 && zimm != 24)
            throw std::runtime_error("Unsupported vtype: this Kites vector subset requires LMUL=1, SEW 8/16/32/64, and undisturbed tail/mask policies");

        const uint8_t new_sew = static_cast<uint8_t>(8u << ((zimm >> 3) & 0x7u));
        const uint8_t vlmax = static_cast<uint8_t>(128u / new_sew);
        uint64_t avl = 0;
        if (rs1 == 0)
        {
            if (vd != 0)
                avl = UINT64_MAX;
            else
            {
                const uint8_t old_vlmax = static_cast<uint8_t>(128u / vector_sew_);
                avl = vector_vl_;
                if (vlmax < old_vlmax)
                    throw std::runtime_error("vsetvli x0, x0 must not reduce VLMAX (reserved form)");
            }
        }
        else
            avl = registers_.ReadGpr(rs1);
        const uint8_t new_vl = static_cast<uint8_t>(std::min<uint64_t>(avl, vlmax));
        vector_sew_ = new_sew;
        vector_vl_ = new_vl;
        if (vd != 0)
        {
            const uint64_t old_value = registers_.ReadGpr(vd);
            registers_.WriteGpr(vd, vector_vl_);
            const uint64_t new_value = registers_.ReadGpr(vd);
            if (old_value != new_value)
                current_delta_.register_changes.push_back({vd, 0, old_value, new_value});
        }
        return;
    }

    const uint8_t funct6 = (current_instruction_ >> 26) & 0x3F;
    const bool vm = ((current_instruction_ >> 25) & 1u) != 0;
    const uint8_t vs2 = (current_instruction_ >> 20) & 0x1F;
    const uint8_t vs1 = rs1;

    // vcpop.m is an OPMVX unary operation with a fixed vs1 field of 10000.
    if (funct3 == 0b110 && funct6 == 0b010000 && vs1 == 0b10000)
    {
        if (!vm)
            throw std::runtime_error("Masked vcpop.m is not supported in this milestone");
        const auto mask_register = registers_.ReadVector(vs2);
        uint64_t count = 0;
        for (size_t lane = 0; lane < vector_vl_; ++lane)
            count += (mask_register[lane / 8] >> (lane % 8)) & 1u;
        if (vd != 0)
        {
            const uint64_t old_value = registers_.ReadGpr(vd);
            registers_.WriteGpr(vd, count);
            const uint64_t new_value = registers_.ReadGpr(vd);
            if (old_value != new_value)
                current_delta_.register_changes.push_back({vd, 0, old_value, new_value});
        }
        return;
    }

    const size_t element_bytes = vector_sew_ / 8;
    const uint64_t element_mask = vector_sew_ == 64 ? UINT64_MAX : ((uint64_t{1} << vector_sew_) - 1u);
    const auto readElement = [element_bytes](const auto &bytes, size_t lane)
    {
        uint64_t value = 0;
        const size_t offset = lane * element_bytes;
        for (size_t byte = 0; byte < element_bytes; ++byte)
            value |= static_cast<uint64_t>(bytes[offset + byte]) << (8 * byte);
        return value;
    };
    const auto writeElement = [element_bytes](auto &bytes, size_t lane, uint64_t value)
    {
        const size_t offset = lane * element_bytes;
        for (size_t byte = 0; byte < element_bytes; ++byte)
            bytes[offset + byte] = static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu);
    };
    const auto signedLess = [this](uint64_t a, uint64_t b)
    {
        const uint64_t sign_bit = uint64_t{1} << (vector_sew_ - 1);
        const bool a_negative = (a & sign_bit) != 0;
        const bool b_negative = (b & sign_bit) != 0;
        if (a_negative != b_negative)
            return a_negative;
        return a < b;
    };

    // OPMVV reductions use vs1[0] as seed and fold active vs2 elements in lane order.
    if (funct3 == 0b010)
    {
        if (!vm)
            throw std::runtime_error("Masked vector reductions are not supported in this milestone");
        const bool supported_reduction =
            funct6 == 0b000000 || funct6 == 0b000001 || funct6 == 0b000010 ||
            funct6 == 0b000011 || funct6 == 0b000101 || funct6 == 0b000111;
        if (!supported_reduction)
            throw std::runtime_error("Unsupported vector reduction instruction");

        const auto source = registers_.ReadVector(vs2);
        const auto seed = registers_.ReadVector(vs1);
        auto result = registers_.ReadVector(vd);
        const auto old_destination = result;
        uint64_t accumulator = readElement(seed, 0);
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            const uint64_t value = readElement(source, lane);
            switch (funct6)
            {
            case 0b000000: accumulator = (accumulator + value) & element_mask; break; // vredsum
            case 0b000001: accumulator &= value; break; // vredand
            case 0b000010: accumulator |= value; break; // vredor
            case 0b000011: accumulator ^= value; break; // vredxor
            case 0b000101: if (signedLess(value, accumulator)) accumulator = value; break; // vredmin
            case 0b000111: if (signedLess(accumulator, value)) accumulator = value; break; // vredmax
            default: break;
            }
        }
        writeElement(result, 0, accumulator & element_mask);
        registers_.WriteVector(vd, result);
        if (old_destination != result)
        {
            RegisterChange change{vd, 3, 0, 0};
            change.old_vector_value = old_destination;
            change.new_vector_value = result;
            current_delta_.register_changes.push_back(change);
        }
        return;
    }

    if (!vm)
        throw std::runtime_error("Masked vector operations are not supported in this milestone");
    if (funct3 != 0b000)
        throw std::runtime_error("Unsupported vector operand format; only OPIVV is implemented");

    const bool comparison = funct6 >= 0b011000 && funct6 <= 0b011011;
    const bool arithmetic_or_logic = funct6 == 0b000000 || funct6 == 0b000010 ||
        funct6 == 0b001001 || funct6 == 0b001010 || funct6 == 0b001011;
    if (!comparison && !arithmetic_or_logic)
        throw std::runtime_error("Unsupported vector arithmetic/logical instruction");

    const auto lhs = registers_.ReadVector(vs2);
    const auto rhs = registers_.ReadVector(vs1);
    auto result = registers_.ReadVector(vd);
    const auto old_destination = result;
    for (size_t lane = 0; lane < vector_vl_; ++lane)
    {
        const uint64_t a = readElement(lhs, lane);
        const uint64_t b = readElement(rhs, lane);
        if (comparison)
        {
            bool predicate = false;
            switch (funct6)
            {
            case 0b011000: predicate = (a == b); break; // vmseq
            case 0b011001: predicate = (a != b); break; // vmsne
            case 0b011010: predicate = (a < b); break;  // vmsltu
            case 0b011011: predicate = signedLess(a, b); break; // vmslt
            default: break;
            }
            const uint8_t bit = static_cast<uint8_t>(1u << (lane % 8));
            if (predicate)
                result[lane / 8] |= bit;
            else
                result[lane / 8] &= static_cast<uint8_t>(~bit);
        }
        else
        {
            uint64_t value = 0;
            switch (funct6)
            {
            case 0b000000: value = (a + b) & element_mask; break;
            case 0b000010: value = (a - b) & element_mask; break;
            case 0b001001: value = a & b; break;
            case 0b001010: value = a | b; break;
            case 0b001011: value = a ^ b; break;
            default: break;
            }
            writeElement(result, lane, value);
        }
    }

    registers_.WriteVector(vd, result);
    if (old_destination != result)
    {
        RegisterChange change{vd, 3, 0, 0};
        change.old_vector_value = old_destination;
        change.new_vector_value = result;
        current_delta_.register_changes.push_back(change);
    }
}
void RVSSProcessor::ExecuteCsr()
{
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint16_t csr = (current_instruction_ >> 20) & 0xFFF;
    uint64_t csr_val = registers_.ReadCsr(csr);

    csr_target_address_ = csr;
    csr_old_value_ = csr_val;
    csr_write_val_ = registers_.ReadGpr(rs1);
    csr_uimm_ = rs1;
}

// TODO: implement writeback for syscalls
void RVSSProcessor::HandleSyscall()
{
    uint64_t syscall_number = registers_.ReadGpr(17);
    switch (syscall_number)
    {
    case SYSCALL_PRINT_INT:
    {
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        std::cout << static_cast<int64_t>(registers_.ReadGpr(10)); // Print signed integer
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_FLOAT:
    { // print float
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        float float_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&float_value, &raw, sizeof(float_value));
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10) << float_value;
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_DOUBLE:
    { // print double
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        double double_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&double_value, &raw, sizeof(double_value));
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10) << double_value;
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_STRING:
    {
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        PrintString(registers_.ReadGpr(10)); // Print string
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        break;
    }
    case SYSCALL_EXIT:
    {
        stop_requested_ = true; // Stop the VM
        if (!globals::vm_as_backend)
        {
            std::cout << "VM_EXIT" << std::endl;
        }
        output_status_ = "VM_EXIT";
        std::cout << "Exited with exit code: " << registers_.ReadGpr(10) << std::endl;
        // stop_requested_ ends the run loop; never call exit() here, it kills the GUI.
        break;
    }
    case SYSCALL_READ:
    { // Read
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 0)
        {
            // Read from stdin
            std::string input;
            {
                std::cout << "VM_STDIN_START" << std::endl;
                output_status_ = "VM_STDIN_START";
                std::unique_lock<std::mutex> lock(input_mutex_);
                input_cv_.wait(lock, [this]() { return !input_queue_.empty(); });
                output_status_ = "VM_STDIN_END";
                std::cout << "VM_STDIN_END" << std::endl;

                input = input_queue_.front();
                input_queue_.pop();
            }

            std::vector<uint8_t> old_bytes_vec(length, 0);
            std::vector<uint8_t> new_bytes_vec(length, 0);

            for (size_t i = 0; i < length; ++i)
            {
                old_bytes_vec[i] = memory_controller_.peekByte(buffer_address + i);
            }

            for (size_t i = 0; i < input.size() && i < length; ++i)
            {
                memory_controller_.writeByte(buffer_address + i, static_cast<uint8_t>(input[i]));
            }
            if (input.size() < length)
            {
                memory_controller_.writeByte(buffer_address + input.size(), '\0');
            }

            for (size_t i = 0; i < length; ++i)
            {
                new_bytes_vec[i] = memory_controller_.peekByte(buffer_address + i);
            }

            current_delta_.memory_changes.push_back({buffer_address, old_bytes_vec, new_bytes_vec});

            uint64_t old_reg = registers_.ReadGpr(10);
            unsigned int reg_index = 10;
            unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR
            uint64_t new_reg =
                std::min(static_cast<uint64_t>(length), static_cast<uint64_t>(input.size()));
            registers_.WriteGpr(10, new_reg);
            if (old_reg != new_reg)
            {
                current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
            }
        }
        else
        {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    case SYSCALL_WRITE:
    { // Write
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 1)
        { // stdout
            std::cout << "VM_STDOUT_START";
            output_status_ = "VM_STDOUT_START";
            uint64_t bytes_printed = 0;
            for (uint64_t i = 0; i < length; ++i)
            {
                char c = memory_controller_.readByte(buffer_address + i);
                // if (c == '\0') {
                //     break;
                // }
                std::cout << c;
                bytes_printed++;
            }
            std::cout << std::flush;
            output_status_ = "VM_STDOUT_END";
            std::cout << "VM_STDOUT_END" << std::endl;

            uint64_t old_reg = registers_.ReadGpr(10);
            unsigned int reg_index = 10;
            unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR
            uint64_t new_reg = std::min(static_cast<uint64_t>(length), bytes_printed);
            registers_.WriteGpr(10, new_reg);
            if (old_reg != new_reg)
            {
                current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
            }
        }
        else
        {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    default:
    {
        std::cerr << "Unknown syscall number: " << syscall_number << std::endl;
        break;
    }
    }
}

void RVSSProcessor::WriteMemory()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        return;
    }

    if (opcode == 0b1010111)
    {
        return; // Vector arithmetic/configuration already updated vector state.
    }

    if (isVectorMemoryInstruction(current_instruction_))
    {
        const bool is_load = opcode == 0x07u;
        const bool vm = ((current_instruction_ >> 25) & 1u) != 0;
        const uint8_t nf = (current_instruction_ >> 29) & 0x7u;
        const bool mew = ((current_instruction_ >> 28) & 1u) != 0;
        const uint8_t mop = (current_instruction_ >> 26) & 0x3u;
        const uint8_t lumop_or_sumop = (current_instruction_ >> 20) & 0x1Fu;
        const uint8_t base_register = (current_instruction_ >> 15) & 0x1Fu;
        const uint8_t vector_register = (current_instruction_ >> 7) & 0x1Fu;
        const size_t element_bytes = vectorMemoryElementBytes(current_instruction_);

        if (!vm || nf != 0 || mew || mop != 0 || lumop_or_sumop != 0)
            throw std::runtime_error(
                "Only unmasked unit-stride one-field vector loads/stores are supported");
        if (element_bytes * 8 != vector_sew_)
            throw std::runtime_error(
                "Vector memory EEW must match the currently configured SEW in this Kites subset");

        const uint64_t base_address = registers_.ReadGpr(base_register);
        const size_t transferred_bytes = static_cast<size_t>(vector_vl_) * element_bytes;

        if (is_load)
        {
            const auto old_destination = registers_.ReadVector(vector_register);
            auto new_destination = old_destination; // inactive tail elements remain unchanged

            for (size_t lane = 0; lane < vector_vl_; ++lane)
            {
                const uint64_t value = readVectorMemoryElement(
                    memory_controller_, base_address + lane * element_bytes, element_bytes);
                const size_t offset = lane * element_bytes;
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    new_destination[offset + byte] =
                        static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu);
            }

            registers_.WriteVector(vector_register, new_destination);
            if (old_destination != new_destination)
            {
                RegisterChange change{vector_register, 3, 0, 0};
                change.old_vector_value = old_destination;
                change.new_vector_value = new_destination;
                current_delta_.register_changes.push_back(change);
            }
        }
        else
        {
            const auto source = registers_.ReadVector(vector_register);
            std::vector<uint8_t> old_bytes;
            std::vector<uint8_t> new_bytes;
            old_bytes.reserve(transferred_bytes);
            new_bytes.reserve(transferred_bytes);

            for (size_t byte = 0; byte < transferred_bytes; ++byte)
                old_bytes.push_back(memory_controller_.peekByte(base_address + byte));

            for (size_t lane = 0; lane < vector_vl_; ++lane)
            {
                uint64_t value = 0;
                const size_t offset = lane * element_bytes;
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    value |= static_cast<uint64_t>(source[offset + byte]) << (8 * byte);

                writeVectorMemoryElement(memory_controller_,
                                         base_address + offset, element_bytes, value);
            }

            for (size_t byte = 0; byte < transferred_bytes; ++byte)
                new_bytes.push_back(memory_controller_.peekByte(base_address + byte));

            if (old_bytes != new_bytes)
                current_delta_.memory_changes.push_back(
                    {base_address, old_bytes, new_bytes});
        }
        return;
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        WriteMemoryFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        WriteMemoryDouble();
        return;
    }

    if (control_unit_.GetMemRead())
    {
        switch (funct3)
        {
        case 0b000:
        { // LB
            memory_result_ = static_cast<int8_t>(memory_controller_.readByte(execution_result_));
            break;
        }
        case 0b001:
        { // LH
            memory_result_ =
                static_cast<int16_t>(memory_controller_.readHalfWord(execution_result_));
            break;
        }
        case 0b010:
        { // LW
            memory_result_ = static_cast<int32_t>(memory_controller_.readWord(execution_result_));
            break;
        }
        case 0b011:
        { // LD
            memory_result_ = memory_controller_.readDoubleWord(execution_result_);
            break;
        }
        case 0b100:
        { // LBU
            memory_result_ = static_cast<uint8_t>(memory_controller_.readByte(execution_result_));
            break;
        }
        case 0b101:
        { // LHU
            memory_result_ =
                static_cast<uint16_t>(memory_controller_.readHalfWord(execution_result_));
            break;
        }
        case 0b110:
        { // LWU
            memory_result_ = static_cast<uint32_t>(memory_controller_.readWord(execution_result_));
            break;
        }
        }
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    // Undo bookkeeping reads use peekByte so they do not show up as cache accesses.

    if (control_unit_.GetMemWrite())
    {
        switch (funct3)
        {
        case 0b000:
        { // SB
            addr = execution_result_;
            old_bytes_vec.push_back(memory_controller_.peekByte(addr));
            memory_controller_.writeByte(execution_result_, registers_.ReadGpr(rs2) & 0xFF);
            new_bytes_vec.push_back(memory_controller_.peekByte(addr));
            break;
        }
        case 0b001:
        { // SH
            addr = execution_result_;
            for (size_t i = 0; i < 2; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeHalfWord(execution_result_, registers_.ReadGpr(rs2) & 0xFFFF);
            for (size_t i = 0; i < 2; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        case 0b010:
        { // SW
            addr = execution_result_;
            for (size_t i = 0; i < 4; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeWord(execution_result_, registers_.ReadGpr(rs2) & 0xFFFFFFFF);
            for (size_t i = 0; i < 4; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        case 0b011:
        { // SD
            addr = execution_result_;
            for (size_t i = 0; i < 8; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeDoubleWord(execution_result_,
                                               registers_.ReadGpr(rs2) & 0xFFFFFFFFFFFFFFFF);
            for (size_t i = 0; i < 8; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteMemoryFloat()
{
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    if (control_unit_.GetMemRead())
    { // FLW
        memory_result_ = memory_controller_.readWord(execution_result_);
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    if (control_unit_.GetMemWrite())
    { // FSW
        addr = execution_result_;
        for (size_t i = 0; i < 4; ++i)
        {
            old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
        uint32_t val = registers_.ReadFpr(rs2) & 0xFFFFFFFF;
        memory_controller_.writeWord(execution_result_, val);
        // new_bytes_vec.push_back(memory_controller_.ReadByte(addr));
        for (size_t i = 0; i < 4; ++i)
        {
            new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteMemoryDouble()
{
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    if (control_unit_.GetMemRead())
    { // FLD
        memory_result_ = memory_controller_.readDoubleWord(execution_result_);
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    if (control_unit_.GetMemWrite())
    { // FSD
        addr = execution_result_;
        for (size_t i = 0; i < 8; ++i)
        {
            old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
        memory_controller_.writeDoubleWord(execution_result_, registers_.ReadFpr(rs2));
        for (size_t i = 0; i < 8; ++i)
        {
            new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteBack()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;
    int32_t imm = ImmGenerator(current_instruction_);

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        return;
    }

    if (opcode == 0b1010111 || isVectorMemoryInstruction(current_instruction_))
    {
        return; // Vector instructions have already updated vector state or memory.
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        WriteBackFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        WriteBackDouble();
        return;
    }
    else if (opcode == 0b1110011)
    { // CSR opcode
        WriteBackCsr();
        return;
    }

    uint64_t old_reg = registers_.ReadGpr(rd);
    unsigned int reg_index = rd;
    unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR

    if (control_unit_.GetRegWrite())
    {
        switch (opcode)
        {
        case 0b0110011: // R-Type
        case 0b0010011: // I-Type
        case 0b0010111: // AUIPC
        case 0b0111011: // R4-Type / OP-32 (ADDW, SUBW, MULW, DIVW, DIVUW, REMW, REMUW, SLLW, SRLW, SRAW)
        case 0b0011011: // I-type OP-32 (ADDIW, SLLIW, SRLIW, SRAIW)
        {
            registers_.WriteGpr(rd, execution_result_);
            break;
        }
        case 0b0000011:
        { // Load
            registers_.WriteGpr(rd, memory_result_);
            break;
        }
        case 0b1100111: // JALR
        case 0b1101111:
        { // JAL
            registers_.WriteGpr(rd, next_pc_);
            break;
        }
        case 0b0110111:
        { // LUI
            registers_.WriteGpr(rd, (imm << 12));
            break;
        }
        default:
            break;
        }
    }

    if (opcode == 0b1101111)
    { // JAL
      // Updated in Execute()
    }
    if (opcode == 0b1100111)
    { // JALR
      // registers_.WriteGpr(rd, return_address_); // Write back to rs1
      // Updated in Execute()
    }

    uint64_t new_reg = registers_.ReadGpr(rd);
    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

void RVSSProcessor::WriteBackFloat()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    if (control_unit_.GetRegWrite())
    {
        // write to GPR
        if (funct7 == 0b1010000 || funct7 == 0b1100000 || funct7 == 0b1110000)
        { // f(eq|lt|le).s, fcvt.(w|wu|l|lu).s
            old_reg = registers_.ReadGpr(rd);
            registers_.WriteGpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 0; // GPR
        }
        // write to FPR
        else if (opcode == 0b0000111)
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, memory_result_);
            new_reg = memory_result_;
            reg_type = 2; // FPR
        }
        else
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 2; // FPR
        }
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

void RVSSProcessor::WriteBackDouble()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    if (control_unit_.GetRegWrite())
    {
        // write to GPR
        if (funct7 == 0b1010001 || funct7 == 0b1100001 || funct7 == 0b1110001)
        { // f(eq|lt|le).d, fcvt.(w|wu|l|lu).d
            old_reg = registers_.ReadGpr(rd);
            registers_.WriteGpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 0; // GPR
        }
        // write to FPR
        else if (opcode == 0b0000111)
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, memory_result_);
            new_reg = memory_result_;
            reg_type = 2; // FPR
        }
        else
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 2; // FPR
        }
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }

    return;
}

void RVSSProcessor::WriteBackCsr()
{
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    uint64_t old_gpr = registers_.ReadGpr(rd);
    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);

    switch (funct3)
    {
    case 0b001:
    { // CSRRW
        registers_.WriteGpr(rd, csr_old_value_);
        registers_.WriteCsr(csr_target_address_, csr_write_val_);
        break;
    }
    case 0b010:
    { // CSRRS
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_write_val_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ | csr_write_val_);
        }
        break;
    }
    case 0b011:
    { // CSRRC
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_write_val_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ & ~csr_write_val_);
        }
        break;
    }
    case 0b101:
    { // CSRRWI
        registers_.WriteGpr(rd, csr_old_value_);
        registers_.WriteCsr(csr_target_address_, csr_uimm_);
        break;
    }
    case 0b110:
    { // CSRRSI
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_uimm_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ | csr_uimm_);
        }
        break;
    }
    case 0b111:
    { // CSRRCI
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_uimm_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ & ~csr_uimm_);
        }
        break;
    }
    }

    uint64_t new_gpr = registers_.ReadGpr(rd);
    uint64_t new_csr = registers_.ReadCsr(csr_target_address_);

    if (old_gpr != new_gpr)
    {
        current_delta_.register_changes.push_back({rd, 0, old_gpr, new_gpr});
    }
    if (old_csr != new_csr)
    {
        current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
    }
}

void RVSSProcessor::DebugRun()
{
    ClearStop();
    while (!stop_requested_ && program_counter_ < program_size_)
    {
        current_delta_.old_pc = program_counter_;
        current_delta_.old_vector_sew = vector_sew_;
        current_delta_.old_vector_vl = vector_vl_;
        if (std::find(breakpoints_.begin(), breakpoints_.end(), program_counter_) ==
            breakpoints_.end())
        {
            Fetch();
            Decode();
            Execute();
            WriteMemory();
            WriteBack();
            instructions_retired_++;
            cycle_s_++;
            std::cout << "Program Counter: " << program_counter_ << std::endl;

            current_delta_.new_pc = program_counter_;
            current_delta_.new_vector_sew = vector_sew_;
            current_delta_.new_vector_vl = vector_vl_;
            // history_.push(current_delta_);
            undo_stack_.push(current_delta_);
            while (!redo_stack_.empty())
            {
                redo_stack_.pop();
            }
            current_delta_ = RVSingleStageStepDelta();
            if (stop_requested_)
            {
                // A syscall that stopped the VM (e.g. exit) already set the status; keep it.
            }
            else if (program_counter_ < program_size_)
            {
                std::cout << "VM_STEP_COMPLETED" << std::endl;
                output_status_ = "VM_STEP_COMPLETED";
            }
            else if (program_counter_ >= program_size_)
            {
                std::cout << "VM_LAST_INSTRUCTION_STEPPED" << std::endl;
                output_status_ = "VM_LAST_INSTRUCTION_STEPPED";
            }
            DumpRegisters(globals::registers_dump_file_path, registers_);
            DumpState(globals::vm_state_dump_file_path);
            // update circuit UI after the debug step
            SetActiveWireNames();
            emit updateCircuitStateSignal(active_wires_);

            //unsigned int delay_ms = vm_config::config.getRunStepDelay();
            // std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        else
        {
            std::cout << "VM_BREAKPOINT_HIT " << program_counter_ << std::endl;
            output_status_ = "VM_BREAKPOINT_HIT";
            break;
        }
    }
    if (!stop_requested_ && program_counter_ >= program_size_)
    {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

void RVSSProcessor::Step()
{
    current_delta_.old_pc = program_counter_;
    current_delta_.old_vector_sew = vector_sew_;
    current_delta_.old_vector_vl = vector_vl_;
    if (program_counter_ < program_size_)
    {
        Fetch();
        Decode();
        Execute();
        WriteMemory();
        WriteBack();
        instructions_retired_++;
        cycle_s_++;
        std::cout << "Program Counter: " << std::hex << program_counter_ << std::dec << std::endl;

        current_delta_.new_pc = program_counter_;
        current_delta_.new_vector_sew = vector_sew_;
        current_delta_.new_vector_vl = vector_vl_;

        // history_.push(current_delta_);

        undo_stack_.push(current_delta_);
        while (!redo_stack_.empty())
        {
            redo_stack_.pop();
        }

        current_delta_ = RVSingleStageStepDelta();

        if (stop_requested_)
        {
            // A syscall that stopped the VM (e.g. exit) already set the status; keep it.
        }
        else if (program_counter_ < program_size_)
        {
            std::cout << "VM_STEP_COMPLETED" << std::endl;
            output_status_ = "VM_STEP_COMPLETED";
        }
        else if (program_counter_ >= program_size_)
        {
            std::cout << "VM_LAST_INSTRUCTION_STEPPED" << std::endl;
            output_status_ = "VM_LAST_INSTRUCTION_STEPPED";
        }
    }
    else if (program_counter_ >= program_size_)
    {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);

}

bool RVSSProcessor::StepAndNotify()
{
    QMutexLocker locker(&step_mutex_);
    if (program_counter_ >= program_size_)
    {
        return false;
    }
    last_executed_pc_ = program_counter_;
    Step();
    SetActiveWireNames();
    setProcessorState();
    emit processorClockedSignal(processor_state_);
    emit updateCircuitStateSignal(active_wires_);
    active_wires_.erase(active_wires_.begin() + always_active_wires_count_,
                        active_wires_.end());
    // clear active wires after emitting signal except always active wires
    return program_counter_ < program_size_;
}

void RVSSProcessor::Undo()
{
    qInfo() << "Attempting to undo last step in rvss";
    if (undo_stack_.empty())
    {
        std::cout << "VM_NO_MORE_UNDO" << std::endl;
        output_status_ = "VM_NO_MORE_UNDO";
        return;
    }

    StepDelta last = undo_stack_.top();
    undo_stack_.pop();

    for (const auto &change : last.register_changes)
    {
        switch (change.reg_type)
        {
        case 0:
        { // GPR
            registers_.WriteGpr(change.reg_index, change.old_value);
            break;
        }
        case 1:
        { // CSR
            registers_.WriteCsr(change.reg_index, change.old_value);
            break;
        }
        case 2:
        { // FPR
            registers_.WriteFpr(change.reg_index, change.old_value);
            break;
        }
        case 3:
        { // 128-bit vector register
            registers_.WriteVector(change.reg_index, change.old_vector_value);
            break;
        }
        default:
            std::cerr << "Invalid register type: " << change.reg_type << std::endl;
            break;
        }
    }

    for (const auto &change : last.memory_changes)
    {
        for (size_t i = 0; i < change.old_bytes_vec.size(); ++i)
        {
            memory_controller_.writeByte(change.address + i, change.old_bytes_vec[i]);
        }
    }

    program_counter_ = last.old_pc;
    vector_sew_ = last.old_vector_sew;
    vector_vl_ = last.old_vector_vl;
    instructions_retired_--;
    cycle_s_--;
    std::cout << "Program Counter: " << program_counter_ << std::endl;

    redo_stack_.push(last);

    output_status_ = "VM_UNDO_COMPLETED";
    std::cout << "VM_UNDO_COMPLETED" << std::endl;

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);

    SetActiveWireNames();
    setProcessorState();
    emit updateCircuitStateSignal(active_wires_);
    emit processorClockedSignal(processor_state_);
    qInfo() << "Undo completed in rvss";
}

void RVSSProcessor::Redo()
{
    qInfo() << "Attempting to redo last undone step in rvss";
    if (redo_stack_.empty())
    {
        std::cout << "VM_NO_MORE_REDO" << std::endl;
        return;
    }

    StepDelta next = redo_stack_.top();
    redo_stack_.pop();

    // if (!history_.can_redo()) {
    //       std::cout << "Nothing to redo.\n";
    //       return;
    //   }

    //   StepDelta next = history_.redo();

    for (const auto &change : next.register_changes)
    {
        switch (change.reg_type)
        {
        case 0:
        { // GPR
            registers_.WriteGpr(change.reg_index, change.new_value);
            break;
        }
        case 1:
        { // CSR
            registers_.WriteCsr(change.reg_index, change.new_value);
            break;
        }
        case 2:
        { // FPR
            registers_.WriteFpr(change.reg_index, change.new_value);
            break;
        }
        case 3:
        { // 128-bit vector register
            registers_.WriteVector(change.reg_index, change.new_vector_value);
            break;
        }
        default:
            std::cerr << "Invalid register type: " << change.reg_type << std::endl;
            break;
        }
    }

    for (const auto &change : next.memory_changes)
    {
        for (size_t i = 0; i < change.new_bytes_vec.size(); ++i)
        {
            memory_controller_.writeByte(change.address + i, change.new_bytes_vec[i]);
        }
    }

    program_counter_ = next.new_pc;
    vector_sew_ = next.new_vector_sew;
    vector_vl_ = next.new_vector_vl;
    instructions_retired_++;
    cycle_s_++;
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
    std::cout << "Program Counter: " << program_counter_ << std::endl;
    undo_stack_.push(next);

    SetActiveWireNames();
    setProcessorState();
    emit updateCircuitStateSignal(active_wires_);
    emit processorClockedSignal(processor_state_);
}

void RVSSProcessor::Reset()
{
    program_counter_ = 0;
    instructions_retired_ = 0;
    cycle_s_ = 0;
    last_breakpoint_pc_ = UINT64_MAX; // Clear breakpoint tracking on reset
    registers_.Reset();
    memory_controller_.reset();
    control_unit_.Reset();
    branch_flag_ = false;
    next_pc_ = 0;
    execution_result_ = 0;
    memory_result_ = 0;

    return_address_ = 0;
    csr_target_address_ = 0;
    csr_old_value_ = 0;
    csr_write_val_ = 0;
    csr_uimm_ = 0;
    vector_sew_ = 8;
    vector_vl_ = 0;
    current_delta_.register_changes.clear();
    current_delta_.memory_changes.clear();
    current_delta_.old_pc = 0;
    current_delta_.new_pc = 0;
    current_delta_.old_vector_sew = 8;
    current_delta_.new_vector_sew = 8;
    current_delta_.old_vector_vl = 0;
    current_delta_.new_vector_vl = 0;
    undo_stack_ = std::stack<StepDelta>();
    redo_stack_ = std::stack<StepDelta>();
}
}//namespaace Kites
