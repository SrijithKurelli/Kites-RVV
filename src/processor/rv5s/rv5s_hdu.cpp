/**
 * @file hazard_detection_unit.cpp
 * @brief Logic for the Hazard Detection Unit (HDU).
 * * This function determines if a data hazard exists between instructions in
 * the ID/EX and IF/ID pipeline registers, and calculates the number of stalls
 * (bubbles) needed based on the simulator's mode (Forwarding ON/OFF).
 * * NOTE: This version includes and uses the full structs from pipeline_registers.h.
 */
#include <algorithm>
#include <cstdint>
#include <iostream>


#include "processor/pipeline_registers.h"

namespace Kites
{
// Define stall count constants
constexpr int STALL_NONE = 0;
constexpr int STALL_ONE_CYCLE = 1;
constexpr int STALL_TWO_CYCLES = 2;

/**
 * @brief Checks for data hazards and returns the number of stalls required.
 * @param if_id_reg The register holding the instruction in the IF/ID stage (the dependent
 * instruction).
 * @param id_ex_reg The register holding the instruction in the ID/EX stage (the source
 * instruction).
 * @param is_forwarding_enabled Flag to switch between 'Forwarding ON' and 'No Forwarding' modes.
 * @return The number of cycles the pipeline should stall (0, 1, or 2).
 */
int check_data_hazard(const IF_ID_Register &if_id_reg, const ID_EX_Register &id_ex_reg,
                      const EX_MEM_Register &ex_mem_reg, bool is_forwarding_enabled)
{
    // --- 1. Extract Source Register Indices from IF/ID Instruction (Dependent) ---
    uint32_t instruction = if_id_reg.instruction;

    // Vector opcode 0x57 reuses rs1/rs2 fields for vector indices except
    // vset* (GPR AVL source) and vmul.vx (GPR scalar source).
    const uint8_t opcode = instruction & 0x7Fu;
    const uint8_t funct3 = (instruction >> 12) & 0x7u;
    const uint8_t funct6 = (instruction >> 26) & 0x3Fu;
    const bool vector_opcode = opcode == 0b1010111;
    const bool vector_config = vector_opcode && funct3 == 0b111;
    const bool vector_immediate_config = vector_config && ((instruction >> 30) == 0b11);
    const bool vector_scalar = vector_opcode && funct3 == 0b110 && funct6 == 0b100101;
    const bool read_gpr_rs1 = !vector_opcode ||
                              (vector_config && !vector_immediate_config) || vector_scalar;
    const bool read_gpr_rs2 = !vector_opcode;

    // GPR Source Registers (rs1/rs2 fields)
    uint8_t if_id_rs1 = (instruction >> 15) & 0x1F;
    uint8_t if_id_rs2 = (instruction >> 20) & 0x1F;

    // FPR Source Registers (frs1/frs2/frs3 fields)
    // Note: F-type instructions reuse the rs1/rs2 bit-fields for frs1/frs2
    uint8_t if_id_frs1 = (instruction >> 15) & 0x1F;
    uint8_t if_id_frs2 = (instruction >> 20) & 0x1F;
    uint8_t if_id_frs3 = (instruction >> 27) & 0x1F; // For FMA (R4 format)

    // --- 2. Check for GPR Hazard (Source writes GPR, Dependent reads GPR) ---
    // Two-cycle hazard: current EX/MEM instruction writes a GPR the IF/ID instruction reads
    bool gpr_hazard_two_cycle = (ex_mem_reg.reg_write && (ex_mem_reg.rd != 0)) &&
                                ((read_gpr_rs1 && ex_mem_reg.rd == if_id_rs1) ||
                                 (read_gpr_rs2 && ex_mem_reg.rd == if_id_rs2));

    // One-cycle hazard: previous EX/MEM instruction (now further down) writes a GPR the IF/ID
    // instruction reads
    bool gpr_hazard_one_cycle =
        (ex_mem_reg.prev_reg_write && (ex_mem_reg.prev_rd != 0)) &&
        ((read_gpr_rs1 && ex_mem_reg.prev_rd == if_id_rs1) ||
         (read_gpr_rs2 && ex_mem_reg.prev_rd == if_id_rs2));

    bool gpr_hazard = gpr_hazard_one_cycle || gpr_hazard_two_cycle;

    // --- 3. Check for FPR Hazard (Source writes FPR, Dependent reads FPR) ---
    // Note: FPR f0 is NOT hardwired to zero (unlike GPR x0), so no frd != 0 check needed.
    // Two-cycle hazard: current EX/MEM instruction writes an FPR the IF/ID instruction reads
    bool fpr_hazard_two_cycle = !vector_opcode &&
        (ex_mem_reg.freg_write) && (ex_mem_reg.frd == if_id_frs1 || ex_mem_reg.frd == if_id_frs2 ||
                                    ex_mem_reg.frd == if_id_frs3);

    // One-cycle hazard: previous EX/MEM instruction writes an FPR the IF/ID instruction reads
    bool fpr_hazard_one_cycle = !vector_opcode &&
        (ex_mem_reg.prev_freg_write) &&
        (ex_mem_reg.prev_frd == if_id_frs1 || ex_mem_reg.prev_frd == if_id_frs2 ||
         ex_mem_reg.prev_frd == if_id_frs3);

    bool fpr_hazard = fpr_hazard_one_cycle || fpr_hazard_two_cycle;

    // --- 4. Determine Stall Count ---
    // Combined hazard flag (either GPR or FPR dependency)
    bool any_hazard_two_cycle = gpr_hazard_two_cycle || fpr_hazard_two_cycle;
    bool any_hazard_one_cycle = gpr_hazard_one_cycle || fpr_hazard_one_cycle;
    bool any_hazard = gpr_hazard || fpr_hazard;

    // Vector ALU forwarding is implemented by the forwarding-enabled variants.
    // The no-forwarding H_NF configuration waits until pending vector writes
    // reach WB before decoding a dependent instruction.
    if (vector_opcode && !vector_config && !is_forwarding_enabled)
    {
        const uint8_t vd = (instruction >> 7) & 0x1Fu;
        const uint8_t vs1 = (instruction >> 15) & 0x1Fu;
        const uint8_t vs2 = (instruction >> 20) & 0x1Fu;
        const bool vm = ((instruction >> 25) & 1u) != 0;
        const bool vector_popcount_mask =
            funct3 == 0b010 && funct6 == 0b010000 && vs1 == 0b10000;
        const bool vector_reduction =
            funct3 == 0b010 &&
            (funct6 == 0b000000 || funct6 == 0b000001 || funct6 == 0b000010 ||
             funct6 == 0b000011 || funct6 == 0b000101 || funct6 == 0b000111);
        const bool vector_unary =
            funct3 == 0b010 && funct6 == 0b010010 &&
            (vs1 == 0b01100 || vs1 == 0b01101 || vs1 == 0b01110);
        const bool vector_multiply = funct3 == 0b010 && funct6 == 0b100101;
        const bool vector_scalar_multiply = funct3 == 0b110 && funct6 == 0b100101;
        const bool reads_vs1_vector = funct3 == 0b000 || vector_reduction || vector_multiply;
        const bool reads_vs2_vector = funct3 == 0b000 || vector_reduction ||
                                      vector_unary || vector_multiply ||
                                      vector_scalar_multiply || vector_popcount_mask;
        const bool reads_mask = !vm;
        const bool writes_vector = !vector_popcount_mask;
        const bool current_raw = ex_mem_reg.vector_write && reads_vs2_vector &&
                                 ex_mem_reg.vector_rd == vs2;
        const bool current_raw_vs1 = ex_mem_reg.vector_write && reads_vs1_vector &&
                                     ex_mem_reg.vector_rd == vs1;
        const bool current_raw_mask = ex_mem_reg.vector_write && reads_mask &&
                                      ex_mem_reg.vector_rd == 0;
        const bool current_waw = ex_mem_reg.vector_write && writes_vector &&
                                 ex_mem_reg.vector_rd == vd;
        const bool previous_raw = ex_mem_reg.prev_vector_write && reads_vs2_vector &&
                                  ex_mem_reg.prev_vector_rd == vs2;
        const bool previous_raw_vs1 = ex_mem_reg.prev_vector_write && reads_vs1_vector &&
                                      ex_mem_reg.prev_vector_rd == vs1;
        const bool previous_raw_mask = ex_mem_reg.prev_vector_write && reads_mask &&
                                       ex_mem_reg.prev_vector_rd == 0;
        const bool previous_waw = ex_mem_reg.prev_vector_write && writes_vector &&
                                  ex_mem_reg.prev_vector_rd == vd;

        if (current_raw || current_raw_vs1 || current_raw_mask || current_waw)
            return STALL_TWO_CYCLES;
        if (previous_raw || previous_raw_vs1 || previous_raw_mask || previous_waw)
            return STALL_ONE_CYCLE;
    }

    if (is_forwarding_enabled)
    {
        // --- Forwarding ON: Detects Load-Use only (1 Stall) ---
        // Load-Use hazard occurs if the producing instruction (ID/EX) is a Load
        // AND the dependent instruction reads that destination register.
        if (id_ex_reg.mem_read)
        {
            // Check if the load destination matches any source of the dependent instruction
            bool gpr_load_use = (id_ex_reg.reg_write && id_ex_reg.rd != 0) &&
                                ((read_gpr_rs1 && id_ex_reg.rd == if_id_rs1) ||
                                 (read_gpr_rs2 && id_ex_reg.rd == if_id_rs2));

            bool fpr_load_use = !vector_opcode && (id_ex_reg.freg_write) &&
                                (id_ex_reg.frd == if_id_frs1 || id_ex_reg.frd == if_id_frs2 ||
                                 id_ex_reg.frd == if_id_frs3);

            if (gpr_load_use || fpr_load_use)
            {
                return STALL_ONE_CYCLE;
            }
        }

        // All other hazards (R-R, F-F, R-Store, F-Store) are solved by the forwarding unit.
        return STALL_NONE;
    }
    else if (!any_hazard)
    {
        return STALL_NONE;
    }
    else if (any_hazard_two_cycle)
    {
        return STALL_TWO_CYCLES;
    }
    else if (any_hazard_one_cycle)
    {
        return STALL_ONE_CYCLE;
    }

    return STALL_NONE;
}
}//namespace Kites