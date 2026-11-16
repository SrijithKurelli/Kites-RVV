/**
 * @file rv5s_processor_h_f.cpp
 * @brief Implementation for the 5-stage pipelined VM (RV5S) in Mode 4: Hazard Detection, With
 * Forwarding.
 * * NOTE: This is a PURE INTEGER (GPR) implementation.
 * * Stall Rule: Only Load-Use GPR dependencies require a 1-cycle stall. All others are solved by
 * forwarding.
 * * Control Hazards (JAL/JALR, B-Type) are AUTOMATICALLY handled by flush/redirect.
 * @author Atharva and Harshit
 */
#include "processor/rv5s/rv5s_processor_h_f.h" // Assuming this header now defines RV5StageProcessorHF
#include "common/instructions.h"
#include "config/config.h"
#include "ui/processor_tab/processor_designs/rv5s_processor_h_f_circuit_scene.h"
#include "processor/alu.h"
#include "processor/pipeline_registers.h"
#include "processor/processor_base.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <tuple>

namespace Kites
{
using namespace alu;

// Define stall count constants (used internally, defined in rv5s_hdu.h)
// constexpr int STALL_NONE = 0;
// constexpr int STALL_ONE_CYCLE = 1;

// --- RV5StageProcessorHF Class Implementation ---

RV5StageProcessorHF::RV5StageProcessorHF() : RV5StageVM_Base()
{
    // Initialize ProcessorBase members
    // program_counter_ = 0;
    // instructions_retired_ = 0;
    // cycle_s_ = 0;
    // stall_cycles_ = 0;

    // // Initialize local members
    // stall_fetch_and_decode_ = false;

    // Reset components and history
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Kites::RV5StageVM_H_F_CircuitScene>();
    connect(this, &ProcessorBase::updateCircuitStateSignal, circuit_scene_.get(),
            &Kites::RV5StageVM_H_F_CircuitScene::updateCircuitState);
#endif
    Reset();

    active_wires_.append("PC_to_IM");
    active_wires_.append("IM_to_P1");
    active_wires_.append("P1_to_P2_PCcarry");
    active_wires_.append("P1_to_Control_RF_andall");
    active_wires_.append("RF_to_P2_UP");
    active_wires_.append("RFdown_to_P2");
    active_wires_.append("PCMux_to_PC");
    active_wires_.append("P2_to_ALU2");
    active_wires_.append("P2_to_P3_MEMControl");
    active_wires_.append("P2_to_P3_WBcontrol");
    active_wires_.append("ALU2_to_P3");
    active_wires_.append("P3_to_P4_WBcontrol");
    active_wires_.append("P2_to_ALUControl");
    active_wires_.append("RdP4_to_FU");
    active_wires_.append("P4EB_to_FWD_unit");
    active_wires_.append("P2_to_FDU_rs1");
    active_wires_.append("P2_to_FDU_RS2");
    active_wires_.append("ALU_to_P3");
    active_wires_.append("P3ALUres_to_DMup");
    active_wires_.append("ALUcontrol_to_ALU");
    active_wires_.append("Fmux1_to_ALU");
    active_wires_.append("Fmux_to_ALUMux_up");
    active_wires_.append("P2_to_FMUX");
    active_wires_.append("P2_to_FMUX2_UP");
    active_wires_.append("P2_to_ALUMux");
    active_wires_.append("P2_to_ALUcontrol_Control");

    always_active_wires_count_ = active_wires_.size();
}

void RV5StageProcessorHF::SetActiveWireNames()
{
    // Clear and populate canonical wires for the Hazard+Forwarding (H_F) circuit
    active_wires_.resize(always_active_wires_count_);
    // Backbone / always-visible wires (names sourced from H_F_Processor.json)

    // Immediate / ALU source selection
    if (id_ex_reg_.alu_src)
    {
        active_wires_.append("Imm_to_P2");
        active_wires_.append("P2Imm_to_ALU2_down");
    }

    // Control signals in ID stage reflected on P2 control wires
    if (id_ex_reg_.mem_write)
    {
        active_wires_.append("P2_to_P3_MEMControl");
    }
    if (id_ex_reg_.reg_write)
    {
        active_wires_.append("P2_to_P3_WBcontrol");
    }

    // Memory control / data path (EX/MEM stage)
    if (ex_mem_reg_.prev_mem_read || ex_mem_reg_.mem_read)
    {
        active_wires_.append("P3_to_DM_Memread");
        active_wires_.append("DMMux_to_DM");
    }
    if (ex_mem_reg_.mem_write)
    {
        active_wires_.append("P3_TO_DM_control_memwrite");
        active_wires_.append("P3ALUres_to_DMup");
    }

    // Register writeback paths (MEM/WB stage)
    if (mem_wb_reg_.prev_reg_write || mem_wb_reg_.reg_write)
    {
        active_wires_.append("P4_wbcontrol_to_WBmux");
        active_wires_.append("RdP4_to_FU");
        active_wires_.append("WBMux_to_RF");
    }

    // Forwarding unit activity: show FWD wires when forwarding is relevant
    if (ex_mem_reg_.reg_write && ex_mem_reg_.rd != 0)
    {
        if (ex_mem_reg_.rd == id_ex_reg_.rs1)
        {
            active_wires_.append("P2_to_FDU_rs1");
            active_wires_.append("P4EB_to_FWD_unit");
            active_wires_.append("FDU_to_FMUX1_cntrl");
            active_wires_.append("FDU_to_fMUX2");
        }
        if (ex_mem_reg_.rd == id_ex_reg_.rs2)
        {
            active_wires_.append("P2_to_FDU_RS2");
            active_wires_.append("P4EB_to_FWD_unit");
            active_wires_.append("FDU_to_FMUX3");
        }
    }

    if (mem_wb_reg_.prev_reg_write && mem_wb_reg_.prev_rd != 0)
    {
        if (mem_wb_reg_.prev_rd == id_ex_reg_.rs1)
        {
            active_wires_.append("P4EB_to_FWD_unit");
            active_wires_.append("FDU_to_FMUX1_cntrl");
        }
        if (mem_wb_reg_.prev_rd == id_ex_reg_.rs2)
        {
            active_wires_.append("P4EB_to_FWD_unit");
            active_wires_.append("FDU_to_FMUX3");
        }
    }

    // Branch / PC selection wires
    if (ex_mem_reg_.prev_branch_taken || ex_mem_reg_.branch_taken)
    {
        active_wires_.append("ALU_zerores_to_P3");
        active_wires_.append("ANDGate_lower_entry");
        active_wires_.append("ANDGATE_to_PCMUX");
        active_wires_.append("P3_to_PCMux");
    }
    else
    {
        active_wires_.append("ALU1_to_PCMuxUp");
    }

    // Hazard Detection Unit (HDU) / stall indicators
    if (stall_fetch_and_decode_ || stall_cycles_ > 0)
    {
        active_wires_.append("HDU_to_P1");
        active_wires_.append("HDUMux_to_P2");
        active_wires_.append("HDU_mux_lowest_toP2");
        active_wires_.append("HDU_to_HDU_mUX");
    }

    // Preserve ordering while removing duplicates
    QList<QString> uniqueList;
    QSet<QString> seen;
    for (const auto &w : active_wires_)
    {
        if (!seen.contains(w))
        {
            uniqueList.append(w);
            seen.insert(w);
        }
    }
    active_wires_ = uniqueList;

    // The scene will read `active_wires_` when `updateCircuitStateSignal` is emitted by callers.
}

void RV5StageProcessorHF::Reset()
{
    RV5StageVM_Base::Reset();
    stall_fetch_and_decode_ = false;
}

void RV5StageProcessorHF::Step()
{
    begin_step_delta();

    // 1. Execute back stages (WB -> MEM -> EX)
    pipeline_writeback();
    pipeline_memory();
    pipeline_execute();

    // --- Hazard Detection and Stall Logic (The core of Mode 4) ---

    int stalls_needed = 0;
    bool currently_stalling = (stall_cycles_ > 0);

    if (currently_stalling)
    {
        // Stall is already active. Decrement counter and continue stall.
        stall_cycles_--;
        stalls_needed = 1; // Indicate we are continuing the stall (1 cycle consumed)
    }
    else
    {
        // Pipeline is free. Check for a NEW hazard using the external HDU function.
        stalls_needed = check_data_hazard(if_id_reg_, id_ex_reg_, ex_mem_reg_,
                                          true /* is_forwarding_enabled */);
        if (stalls_needed > 0)
        {
            // Start the stall: stalls_needed cycles total. 1 cycle is handled now.
            stall_cycles_ = stalls_needed - 1;
        }
    }

    // 2. Process Decode/Fetch based on stall status
    if (stalls_needed > 0)
    {
        // STALL: Freeze IF/ID (by preventing its update) and inject NOP into ID/EX
        id_ex_reg_.reset();
        // Skip pipeline_decode() - The instruction in IF/ID stays put.
        stall_fetch_and_decode_ = true;
    }
    else
    {
        // NO STALL: Advance pipeline normally
        pipeline_decode();
        // vsetvli/vsetivli return the selected VL in the integer rd register.
        if ((id_ex_reg_.instruction & 0x7Fu) == 0b1010111 &&
            ((id_ex_reg_.instruction >> 12) & 0x7u) == 0b111u)
            id_ex_reg_.reg_write = id_ex_reg_.rd != 0;
        stall_fetch_and_decode_ = false;
    }

    // 3. Fetch and PC update
    fetch_and_advance_pc(stall_fetch_and_decode_);

    cycle_s_++; // One clock cycle has passed

    finalize_step_delta();
}

// --- Pipeline Stage Implementations (Mode 4: H_F) ---

void RV5StageProcessorHF::pipeline_fetch()
{
    // If a stall is active, we prevent IF/ID from updating.
    if (stall_fetch_and_decode_)
    {
        // IF/ID register is intentionally NOT updated, holding the stalled instruction.
        return;
    }

    if (program_counter_ < program_size_)
    {
        if_id_reg_.instruction = memory_controller_.readInstruction(program_counter_);
        if_id_reg_.pc = program_counter_;
    }
    else
    {
        if_id_reg_.reset();
    }
}

void RV5StageProcessorHF::pipeline_execute()
{
    uint32_t instruction = id_ex_reg_.instruction;
    bool overflow = false;
    uint64_t alu_result = 0;
    bool vector_write = false;
    uint8_t vector_rd = 0;
    std::array<uint8_t, 16> vector_result{};

    bool is_f_instruction = instruction_set::isFInstruction(instruction);
    bool is_d_instruction = instruction_set::isDInstruction(instruction);
    const uint8_t opcode = instruction & 0x7Fu;
    const uint8_t funct3 = (instruction >> 12) & 0x7u;

    if (opcode == 0b1010111)
    {
        const uint8_t rd = (instruction >> 7) & 0x1Fu;
        const uint8_t rs1 = (instruction >> 15) & 0x1Fu;
        const uint8_t funct6 = (instruction >> 26) & 0x3Fu;

        if (funct3 == 0b111)
        {
            // Configuration subset: vsetvli and vsetivli, LMUL=1, SEW 8/16/32/64.
            const uint32_t format = instruction >> 30;
            if (format == 0b10)
                throw std::runtime_error("vsetvl is not supported by the H_F vector subset");
            if (format == 0b01)
                throw std::runtime_error("Reserved vector configuration format");
            const bool immediate_avl = format == 0b11;
            const uint16_t zimm = static_cast<uint16_t>(
                (instruction >> 20) & (immediate_avl ? 0x3FFu : 0x7FFu));
            if (zimm != 0 && zimm != 8 && zimm != 16 && zimm != 24)
                throw std::runtime_error(
                    "Unsupported vector configuration: H_F supports LMUL=1, SEW 8/16/32/64");

            const uint8_t new_sew = static_cast<uint8_t>(8u << ((zimm >> 3) & 0x7u));
            const uint8_t vlmax = static_cast<uint8_t>(128u / new_sew);
            uint64_t avl = 0;
            if (immediate_avl)
                avl = rs1;
            else if (rs1 == 0)
            {
                if (rd != 0)
                    avl = UINT64_MAX;
                else
                {
                    const uint8_t old_vlmax = static_cast<uint8_t>(128u / vector_sew_);
                    if (vlmax != old_vlmax)
                        throw std::runtime_error(
                            "vsetvli x0, x0 requires unchanged VLMAX (reserved form)");
                    avl = vector_vl_;
                }
            }
            else
                avl = forward_gpr(rs1, id_ex_reg_.reg1_data);

            vector_sew_ = new_sew;
            vector_vl_ = static_cast<uint8_t>(std::min<uint64_t>(avl, vlmax));
            if (rd != 0)
                alu_result = vector_vl_;
        }
        else
        {
            // This pipeline milestone supports masked/unmasked vadd.vv, vsub.vv,
            // vmul.vv and vmul.vx. Vector results commit in WB; the HDU inserts
            // a bubble when a younger instruction consumes a pending vector result.
            const bool vector_vector = funct3 == 0b000 ||
                                       (funct3 == 0b010 && funct6 == 0b100101);
            const bool vector_scalar = funct3 == 0b110 && funct6 == 0b100101;
            const bool supported =
                (funct3 == 0b000 && (funct6 == 0b000000 || funct6 == 0b000010)) ||
                (funct3 == 0b010 && funct6 == 0b100101) || vector_scalar;
            if (!supported)
                throw std::runtime_error("Unsupported vector operation in the H_F pipeline subset");
            if (vector_sew_ != 8 && vector_sew_ != 16 &&
                vector_sew_ != 32 && vector_sew_ != 64)
                throw std::runtime_error("Invalid SEW in H_F vector state");
            if (vector_vl_ > 128u / vector_sew_)
                throw std::runtime_error("VL exceeds VLMAX in H_F vector state");

            const uint8_t vd = rd;
            const uint8_t vs2 = (instruction >> 20) & 0x1Fu;
            const uint8_t vs1 = rs1;
            const bool vm = ((instruction >> 25) & 1u) != 0;
            if (!vm && (vd == 0 || vs2 == 0 || (vector_vector && vs1 == 0)))
                throw std::runtime_error("Masked H_F vector operands cannot overlap v0");

            const size_t element_bytes = vector_sew_ / 8u;
            const uint64_t element_mask = vector_sew_ == 64
                ? UINT64_MAX : ((uint64_t{1} << vector_sew_) - 1u);
            const auto read_element = [element_bytes](const std::array<uint8_t, 16> &bytes,
                                                       size_t lane) {
                uint64_t value = 0;
                const size_t offset = lane * element_bytes;
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    value |= static_cast<uint64_t>(bytes[offset + byte]) << (8u * byte);
                return value;
            };
            const auto write_element = [element_bytes](std::array<uint8_t, 16> &bytes,
                                                        size_t lane, uint64_t value) {
                const size_t offset = lane * element_bytes;
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    bytes[offset + byte] = static_cast<uint8_t>((value >> (8u * byte)) & 0xFFu);
            };

            const auto source2 = registers_.ReadVector(vs2);
            const auto source1 = vector_vector ? registers_.ReadVector(vs1)
                                               : std::array<uint8_t, 16>{};
            const auto predicate = vm ? std::array<uint8_t, 16>{} : registers_.ReadVector(0);
            vector_result = registers_.ReadVector(vd);
            const uint64_t scalar = vector_scalar
                ? (forward_gpr(vs1, id_ex_reg_.reg1_data) & element_mask) : 0;

            for (size_t lane = 0; lane < vector_vl_; ++lane)
            {
                if (!vm && ((predicate[lane / 8u] >> (lane % 8u)) & 1u) == 0)
                    continue;

                const uint64_t a = read_element(source2, lane);
                const uint64_t b = vector_scalar ? scalar : read_element(source1, lane);
                uint64_t value = 0;
                if (funct6 == 0b000000)
                    value = (a + b) & element_mask;
                else if (funct6 == 0b000010)
                    value = (a - b) & element_mask;
                else
                    value = (a * b) & element_mask;
                write_element(vector_result, lane, value);
            }

            vector_write = true;
            vector_rd = vd;
        }
    }
    else if (is_f_instruction)
    {
        alu_result = pipeline_execute_float();
    }
    else if (is_d_instruction)
    {
        alu_result = pipeline_execute_double();
    }
    else
    {
        uint64_t alu_in1 = id_ex_reg_.reg1_data;
        uint64_t alu_in2 = id_ex_reg_.reg2_data;

        // --- GPR FORWARDING LOGIC ---
        uint8_t forward_a = 0; // 10 = EX/MEM, 01 = MEM/WB
        uint8_t forward_b = 0;

        // **Priority 1: EX/MEM Forward** (R-R, R-Store hazard solved)
        if (ex_mem_reg_.reg_write && (ex_mem_reg_.rd != 0))
        {
            if (ex_mem_reg_.rd == id_ex_reg_.rs1)
                forward_a = 2;
            if (ex_mem_reg_.rd == id_ex_reg_.rs2)
                forward_b = 2;
        }

        // **Priority 2: MEM/WB Forward** (R-R, L-Store hazard solved)
        if (mem_wb_reg_.prev_reg_write && (mem_wb_reg_.prev_rd != 0))
        {
            // Forward A from MEM/WB unless EX/MEM is already forwarding
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs1 && forward_a != 2)
                forward_a = 1;
            // Forward B from MEM/WB unless EX/MEM is already forwarding
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs2 && forward_b != 2)
                forward_b = 1;
        }

        // --- FORWARDING APPLICATION ---

        if (forward_a == 2)
        {
            alu_in1 = ex_mem_reg_.alu_result;
        }
        else if (forward_a == 1)
        {
            alu_in1 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        if (forward_b == 2)
        {
            alu_in2 = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        {
            alu_in2 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        // Re-apply immediate value check (must happen AFTER forwarding application)
        if (id_ex_reg_.alu_src)
        {
            alu_in2 = static_cast<uint64_t>(id_ex_reg_.imm);
        }

        // --- EXECUTION ---
        alu::AluOp alu_operation = control_unit_.GetAluSignal(instruction, id_ex_reg_.alu_op > 0);
        std::tie(alu_result, overflow) = alu::Alu::execute(alu_operation, alu_in1, alu_in2);
        // temporary fix for lui instruction

        if ((id_ex_reg_.instruction & 0b1111111) == 0b0110111) // lui
        {
            alu_result = static_cast<uint64_t>(id_ex_reg_.imm << 12);
        }
        else if ((id_ex_reg_.instruction & 0b1111111) == 0b0010111) // auipc
        {
            alu_result = id_ex_reg_.pc + static_cast<uint64_t>(static_cast<int64_t>(
                             static_cast<int32_t>(static_cast<uint32_t>(id_ex_reg_.imm) << 12)));
        }

        // Latch data for EX/MEM Register

        // Store Data: CRITICAL FORWARDING - Store data must also be forwarded!
        uint64_t store_data = id_ex_reg_.reg2_data;
        if (forward_b == 2)
        {
            store_data = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        {
            store_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                     : mem_wb_reg_.prev_alu_result;
        }

        ex_mem_reg_.reg2_data = store_data;
    }

    ex_mem_reg_.prev_reg_write = ex_mem_reg_.reg_write;
    ex_mem_reg_.prev_rd = ex_mem_reg_.rd;
    ex_mem_reg_.prev_freg_write = ex_mem_reg_.freg_write;
    ex_mem_reg_.prev_frd = ex_mem_reg_.frd;
    ex_mem_reg_.pc = id_ex_reg_.pc;
    ex_mem_reg_.instruction = id_ex_reg_.instruction;
    ex_mem_reg_.alu_result = alu_result;
    ex_mem_reg_.f_alu_result = (is_f_instruction || is_d_instruction) ? alu_result : 0;
    ex_mem_reg_.vector_result = vector_result;
    ex_mem_reg_.vector_rd = vector_rd;
    ex_mem_reg_.vector_write = vector_write;
    ex_mem_reg_.rd = id_ex_reg_.rd;
    ex_mem_reg_.frd = id_ex_reg_.frd;
    // NOTE: reg2_data and freg2_data are set inside the integer/FP execute
    // branches above with proper forwarding applied — do NOT overwrite here.
    ex_mem_reg_.reg_write = id_ex_reg_.reg_write;
    ex_mem_reg_.freg_write = id_ex_reg_.freg_write;
    ex_mem_reg_.mem_to_reg = id_ex_reg_.mem_to_reg;
    ex_mem_reg_.prev_mem_read = ex_mem_reg_.mem_read;
    ex_mem_reg_.prev_mem_write = ex_mem_reg_.mem_write;
    ex_mem_reg_.mem_read = id_ex_reg_.mem_read;
    ex_mem_reg_.mem_write = id_ex_reg_.mem_write;
    ex_mem_reg_.prev_branch_taken = ex_mem_reg_.branch_taken;
    ex_mem_reg_.branch_taken = false;
    ex_mem_reg_.branch_target_pc = 0;

    uint8_t opcode = instruction & 0b1111111;

    // --- Control Hazard Logic (Automated) ---

    // Conditional Branch Check (B-type)
    if (id_ex_reg_.branch && opcode == 0b1100011)
    {
        bool condition_met = false;
        uint8_t funct3 = (instruction >> 12) & 0x7;

        switch (funct3)
        {
        case 0b000:
            condition_met = (alu_result == 0);
            break; // BEQ
        case 0b001:
            condition_met = (alu_result != 0);
            break; // BNE
        case 0b100:
            condition_met = (alu_result == 1);
            break; // BLT
        case 0b101:
            condition_met = (alu_result == 0);
            break; // BGE
        case 0b110:
            condition_met = (alu_result == 1);
            break; // BLTU
        case 0b111:
            condition_met = (alu_result == 0);
            break; // BGEU
        }

        if (condition_met)
        {
            ex_mem_reg_.branch_taken = true;
            ex_mem_reg_.branch_target_pc = id_ex_reg_.pc + id_ex_reg_.imm;
        }
    }
    // Unconditional Jump Check (JAL/JALR: 1-cycle penalty)
    else if (opcode == 0b1101111 || opcode == 0b1100111)
    {
        uint64_t jump_target;
        if (opcode == 0b1101111)
        {
            jump_target = id_ex_reg_.pc + id_ex_reg_.imm; // JAL
        }
        else
        {
            jump_target = alu_result & ~1;              // JALR (ALU result is Reg + Imm)
            ex_mem_reg_.alu_result = id_ex_reg_.pc + 4; // Set link address (PC+4)
        }

        redirect_pc(jump_target);
        if_id_reg_.reset();
        ex_mem_reg_.branch_taken = true;
    }
}

void RV5StageProcessorHF::handle_syscall()
{
    if ((id_ex_reg_.instruction & 0x7F) == 0b1110011 &&
        ((id_ex_reg_.instruction >> 12) & 0x7) == 0b000)
    {
        RequestStop();
        output_status_ = "ECALL_EXIT";
        // DumpState("vm_state.json");
    }
}

uint64_t RV5StageProcessorHF::pipeline_execute_float()
{
    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t funct7 = (instruction >> 25) & 0b1111111;
    uint8_t rm = funct3;

    uint8_t fcsr_status = 0;
    uint64_t alu_result = 0;

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = id_ex_reg_.freg1_data;
    uint64_t reg2_value = id_ex_reg_.freg2_data;
    uint64_t reg3_value = id_ex_reg_.freg3_data;

    if (instruction_set::fpReadsGprRs1(instruction))
    {
        reg1_value = forward_gpr(id_ex_reg_.rs1, id_ex_reg_.reg1_data);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // --- FORWARDING for FPR sources (frs1/frs2/frs3) ---
    uint8_t fwd_a = 0; // 2 = EX/MEM, 1 = MEM/WB
    uint8_t fwd_b = 0;
    uint8_t fwd_c = 0;

    // EX/MEM forwarding (highest priority)
    if (ex_mem_reg_.freg_write) // f0 is a normal register: no frd != 0 check
    {
        if (ex_mem_reg_.frd == id_ex_reg_.frs1)
            fwd_a = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs2)
            fwd_b = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs3)
            fwd_c = 2;
    }

    // MEM/WB forwarding (lower priority)
    if (mem_wb_reg_.prev_freg_write) // f0 is a normal register
    {
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs1 && fwd_a != 2)
            fwd_a = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs2 && fwd_b != 2)
            fwd_b = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs3 && fwd_c != 2)
            fwd_c = 1;
    }

    // Apply forwarding to source values
    if (fwd_a == 2)
        reg1_value = ex_mem_reg_.f_alu_result;
    else if (fwd_a == 1)
        reg1_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_b == 2)
        reg2_value = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        reg2_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_c == 2)
        reg3_value = ex_mem_reg_.f_alu_result;
    else if (fwd_c == 1)
        reg3_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    // Re-apply GPR-read override (for conversion ops) after forwarding decision: if instruction
    // requires GPR, use it.
    if (instruction_set::fpReadsGprRs1(instruction))
    {
        reg1_value = forward_gpr(id_ex_reg_.rs1, id_ex_reg_.reg1_data);
    }

    // Re-apply immediate if ALUSrc was set (immediates take precedence for reg2)
    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // Forward store data for FPR stores (frs2)
    uint64_t fstore_data = id_ex_reg_.freg2_data;
    if (fwd_b == 2)
        fstore_data = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        fstore_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                  : mem_wb_reg_.prev_f_alu_result;

    ex_mem_reg_.freg2_data = fstore_data;
    // Execute FP operation
    alu::AluOp aluOperation = control_unit_.GetAluSignal(instruction, id_ex_reg_.alu_op > 0);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    registers_.WriteCsr(0x003, fcsr_status);

    return alu_result;
}

uint64_t RV5StageProcessorHF::pipeline_execute_double()
{
    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t funct7 = (instruction >> 25) & 0b1111111;
    uint8_t rm = funct3;

    uint8_t fcsr_status = 0;
    uint64_t alu_result = 0;

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = id_ex_reg_.freg1_data;
    uint64_t reg2_value = id_ex_reg_.freg2_data;
    uint64_t reg3_value = id_ex_reg_.freg3_data;

    if (instruction_set::fpReadsGprRs1(instruction))
    {
        reg1_value = forward_gpr(id_ex_reg_.rs1, id_ex_reg_.reg1_data);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // --- FORWARDING for FPR sources (frs1/frs2/frs3) ---
    uint8_t fwd_a = 0; // 2 = EX/MEM, 1 = MEM/WB
    uint8_t fwd_b = 0;
    uint8_t fwd_c = 0;

    if (ex_mem_reg_.freg_write) // f0 is a normal register: no frd != 0 check
    {
        if (ex_mem_reg_.frd == id_ex_reg_.frs1)
            fwd_a = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs2)
            fwd_b = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs3)
            fwd_c = 2;
    }

    if (mem_wb_reg_.prev_freg_write) // f0 is a normal register
    {
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs1 && fwd_a != 2)
            fwd_a = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs2 && fwd_b != 2)
            fwd_b = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs3 && fwd_c != 2)
            fwd_c = 1;
    }

    if (fwd_a == 2)
        reg1_value = ex_mem_reg_.f_alu_result;
    else if (fwd_a == 1)
        reg1_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_b == 2)
        reg2_value = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        reg2_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_c == 2)
        reg3_value = ex_mem_reg_.f_alu_result;
    else if (fwd_c == 1)
        reg3_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    // Re-apply GPR-read override for specific instructions
    if (instruction_set::fpReadsGprRs1(instruction))
    {
        reg1_value = forward_gpr(id_ex_reg_.rs1, id_ex_reg_.reg1_data);
    }

    // Re-apply immediate for reg2
    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // Forward store data for FPR stores
    uint64_t fstore_data = id_ex_reg_.freg2_data;
    if (fwd_b == 2)
        fstore_data = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        fstore_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                  : mem_wb_reg_.prev_f_alu_result;

    ex_mem_reg_.freg2_data = fstore_data;

    alu::AluOp alu_operation = control_unit_.GetAluSignal(instruction, id_ex_reg_.alu_op > 0);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::dfpexecute(alu_operation, reg1_value, reg2_value, reg3_value, rm);

    registers_.WriteCsr(0x003, fcsr_status);

    return alu_result;
}
}//namespace Kites
