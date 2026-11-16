#include "common/debug_colors.h"
#include "common/instructions.h"
#include "processor/rv5s/rv5s_processor_base.h"
#include "config/config.h"
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Kites
{
void RV5StageVM_Base::Run()
{
    ClearStop();
    while (!stop_requested_ && (program_counter_ < program_size_ || !is_pipeline_drained()))
    {
        if (last_breakpoint_pc_ && program_counter_ != *last_breakpoint_pc_)
        {
            last_breakpoint_pc_.reset();
        }

        // if we hit a break point (and it's a new PC), we pause execution
        // This prevents firing the same breakpoint multiple times during stalls
        if (std::find(breakpoints_.begin(), breakpoints_.end(), program_counter_) !=
                breakpoints_.end() && program_counter_ != last_breakpoint_pc_)
        {
            pause_requested_ = true;
            last_breakpoint_pc_ = program_counter_;
            emit processorPausedAtBreakpointSignal();
        }

        {
            QMutexLocker locker(&pause_mutex_);
            // using while because thie wait can be interrupted by spurious wakeups
            while (pause_requested_ && !stop_requested_)
            {
                pause_wait_condition_.wait(&pause_mutex_);
            }

            if (stop_requested_) // if we were requested to stop while paused
                break;
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
        // we emit the processor clocked signal once more to update the ui
        // this will get rid of any highlights as they pause when we stop
        // before execution is complete
    }
}

bool RV5StageVM_Base::StepAndNotify()
{
    QMutexLocker locker(&step_mutex_);
    if (program_counter_ >= program_size_ && is_pipeline_drained())
    {
        return false;
    }
    last_executed_pc_ = id_ex_reg_.pc; // this pc will be executed in this step,
    //so we set it as the last executed pc before we step
    Step();
    SetActiveWireNames();
    cpi_ = instructions_retired_
               ? static_cast<float>(cycle_s_) / static_cast<float>(instructions_retired_)
               : 0.0f;
    ipc_ = cycle_s_ ? static_cast<float>(instructions_retired_) / static_cast<float>(cycle_s_)
                    : 0.0f;

    setProcessorState();
    emit updateCircuitStateSignal(active_wires_);
    emit processorClockedSignal(processor_state_);
    return program_counter_ < program_size_ || !is_pipeline_drained();
}

void RV5StageVM_Base::DebugRun()
{
    ClearStop();
    while (!stop_requested_ && (program_counter_ < program_size_ || !is_pipeline_drained()))
    {
#ifdef VM_DEBUG_PRINTS
        print_pipeline_registers_debug();
#endif
        Step();
        std::cout << "Cycle: " << cycle_s_ << " | PC: 0x" << std::hex << program_counter_
                  << std::dec << std::endl;
    }
#ifdef VM_DEBUG_PRINTS
    print_pipeline_registers_debug();
#endif
}

void RV5StageVM_Base::Reset()
{
    program_counter_      = 0;
    instructions_retired_ = 0;
    cycle_s_              = 0;
    stall_cycles_         = 0;
    pc_redirected_        = false;
    last_breakpoint_pc_.reset(); // Clear breakpoint tracking on reset

    registers_.Reset();
    memory_controller_.reset();
    control_unit_.Reset();
    vector_sew_ = 8;
    vector_vl_ = 0;

    if_id_reg_.reset();
    id_ex_reg_.reset();
    ex_mem_reg_.reset();
    mem_wb_reg_.reset();
    current_delta_ = RV5StageStepDelta{};
    undo_stack_ = std::stack<RV5StageStepDelta>();
    redo_stack_ = std::stack<RV5StageStepDelta>();
    processor_state_.reset();
}

void RV5StageVM_Base::begin_step_delta()
{
    current_delta_                           = RV5StageStepDelta{};
    current_delta_.old_pc                    = program_counter_;
    current_delta_.old_cycle                 = cycle_s_;
    current_delta_.old_stall_cycles          = stall_cycles_;
    current_delta_.old_instructions_retired  = instructions_retired_;
    current_delta_.old_branch_mispredictions = branch_mispredictions_;
    current_delta_.old_vector_sew = vector_sew_;
    current_delta_.old_vector_vl = vector_vl_;

    current_delta_.pipeline_register_change.old_if_id_reg  = if_id_reg_;
    current_delta_.pipeline_register_change.old_id_ex_reg  = id_ex_reg_;
    current_delta_.pipeline_register_change.old_ex_mem_reg = ex_mem_reg_;
    current_delta_.pipeline_register_change.old_mem_wb_reg = mem_wb_reg_;
}

void RV5StageVM_Base::finalize_step_delta()
{
    current_delta_.new_pc                    = program_counter_;
    current_delta_.new_cycle                 = cycle_s_;
    current_delta_.new_stall_cycles          = stall_cycles_;
    current_delta_.new_instructions_retired  = instructions_retired_;
    current_delta_.new_branch_mispredictions = branch_mispredictions_;
    current_delta_.new_vector_sew = vector_sew_;
    current_delta_.new_vector_vl = vector_vl_;

    current_delta_.pipeline_register_change.new_if_id_reg  = if_id_reg_;
    current_delta_.pipeline_register_change.new_id_ex_reg  = id_ex_reg_;
    current_delta_.pipeline_register_change.new_ex_mem_reg = ex_mem_reg_;
    current_delta_.pipeline_register_change.new_mem_wb_reg = mem_wb_reg_;

    undo_stack_.push(current_delta_);
    while (!redo_stack_.empty())
    {
        redo_stack_.pop();
    }
}

void RV5StageVM_Base::setProcessorState()
{
    // processor_state_.programCounters[toIndex(PipelineStage::IF)]  = program_counter_;
    // processor_state_.programCounters[toIndex(PipelineStage::ID)]  = if_id_reg_.pc;
    // processor_state_.programCounters[toIndex(PipelineStage::EX)]  = id_ex_reg_.pc;
    // processor_state_.programCounters[toIndex(PipelineStage::MEM)] = ex_mem_reg_.pc;
    // processor_state_.programCounters[toIndex(PipelineStage::WB)]  = mem_wb_reg_.pc;
    processor_state_.programCounters = 
    {program_counter_, if_id_reg_.pc, id_ex_reg_.pc, ex_mem_reg_.pc, mem_wb_reg_.pc};
}

void RV5StageVM_Base::pipeline_decode()
{
    uint32_t instruction = if_id_reg_.instruction;
    if (instruction == NOP)
    {
        // Pass through fields as needed
        id_ex_reg_.pc          = if_id_reg_.pc;
        id_ex_reg_.instruction = instruction;
        id_ex_reg_.imm         = 0;
        id_ex_reg_.rs1         = id_ex_reg_.rs2 = id_ex_reg_.rd = 0;
        id_ex_reg_.reg1_data   = 0;
        id_ex_reg_.reg2_data   = 0;

        // Clear FPR fields so stale values don't poison forwarding
        id_ex_reg_.frs1 = id_ex_reg_.frs2 = id_ex_reg_.frs3 = id_ex_reg_.frd = 0;
        id_ex_reg_.freg1_data = 0;
        id_ex_reg_.freg2_data = 0;
        id_ex_reg_.freg3_data = 0;

        // Critically: zero *all* control signals so downstream stages are idle
        id_ex_reg_.reg_write  = false;
        id_ex_reg_.freg_write = false;
        id_ex_reg_.branch     = false;
        id_ex_reg_.alu_src    = false;
        id_ex_reg_.mem_read   = false;
        id_ex_reg_.mem_write  = false;
        id_ex_reg_.mem_to_reg = false;
        id_ex_reg_.alu_op     = 0;
        return;
    }
    // Control Unit: Generate signals based on the instruction
    control_unit_.SetControlSignals(instruction);

    // Latch data for the ID/EX register
    id_ex_reg_.pc = if_id_reg_.pc;
    id_ex_reg_.instruction = instruction;
    id_ex_reg_.imm = ImmGenerator(instruction);

    // Extract register numbers
    id_ex_reg_.rs1 = (instruction >> 15) & 0x1F;
    id_ex_reg_.rs2 = (instruction >> 20) & 0x1F;
    id_ex_reg_.rd = (instruction >> 7) & 0x1F;

    // Extract FP register indices for F/D instructions
    const bool is_fp = instruction_set::isFInstruction(instruction) ||
                       instruction_set::isDInstruction(instruction);
    if (is_fp)
    {
        id_ex_reg_.frs1 = (instruction >> 15) & 0x1F;
        id_ex_reg_.frs2 = (instruction >> 20) & 0x1F;
        id_ex_reg_.frs3 = (instruction >> 27) & 0x1F;
        id_ex_reg_.frd = (instruction >> 7) & 0x1F;

        id_ex_reg_.freg1_data = registers_.ReadFpr(id_ex_reg_.frs1);
        id_ex_reg_.freg2_data = registers_.ReadFpr(id_ex_reg_.frs2);
        id_ex_reg_.freg3_data = registers_.ReadFpr(id_ex_reg_.frs3);

        // Loads/stores (base address), fcvt-from-int and fmv.*.x read a GPR via rs1.
        // Latch it here so EX can forward it like any integer operand. rs2 is an FPR.
        id_ex_reg_.reg1_data = registers_.ReadGpr(id_ex_reg_.rs1);
        id_ex_reg_.reg2_data = 0;
    }
    else
    {
        id_ex_reg_.frs1 = id_ex_reg_.frs2 = id_ex_reg_.frs3 = id_ex_reg_.frd = 0;
        id_ex_reg_.reg1_data = registers_.ReadGpr(id_ex_reg_.rs1);
        id_ex_reg_.reg2_data = registers_.ReadGpr(id_ex_reg_.rs2);
    }

    // Pass all control signals to the next stage.
    // reg_write = writes a GPR; freg_write = writes an FPR. They are mutually exclusive, so
    // GPR forwarding/hazard logic never acts on an FP destination and vice versa.
    id_ex_reg_.reg_write  = is_fp ? instruction_set::fpWritesGpr(instruction)
                                  : control_unit_.GetRegWrite();
    if ((instruction & 0x7Fu) == 0x57u)
    {
        const uint8_t vector_funct3 = (instruction >> 12) & 0x7u;
        const uint8_t vector_funct6 = (instruction >> 26) & 0x3Fu;
        const uint8_t vector_vs1 = (instruction >> 15) & 0x1Fu;
        const bool writes_vl = vector_funct3 == 0b111;
        const bool writes_mask_count = vector_funct3 == 0b010 &&
                                       vector_funct6 == 0b010000 && vector_vs1 == 0b10000;
        id_ex_reg_.reg_write = id_ex_reg_.rd != 0 && (writes_vl || writes_mask_count);
    }
    id_ex_reg_.freg_write = instruction_set::fpWritesFpr(instruction);
    id_ex_reg_.branch     = control_unit_.GetBranch();
    id_ex_reg_.alu_src    = control_unit_.GetAluSrc();
    id_ex_reg_.mem_read   = control_unit_.GetMemRead();
    id_ex_reg_.mem_write  = control_unit_.GetMemWrite();
    id_ex_reg_.mem_to_reg = control_unit_.GetMemToReg();
    id_ex_reg_.alu_op     = control_unit_.GetAluOp();
    if (isVectorMemoryInstruction(instruction))
    {
        id_ex_reg_.reg_write = false;
        id_ex_reg_.freg_write = false;
        id_ex_reg_.mem_read = false;
        id_ex_reg_.mem_write = false;
        id_ex_reg_.mem_to_reg = false;
        id_ex_reg_.alu_src = false;
    }
}


RV5VectorExecutionResult RV5StageVM_Base::execute_vector_instruction(bool forwarding_enabled)
{
    constexpr uint8_t kVectorOpcode = 0x57u;
    RV5VectorExecutionResult execution;
    const uint32_t instruction = id_ex_reg_.instruction;
    const uint8_t opcode = instruction & 0x7Fu;
    const bool vector_memory = isVectorMemoryInstruction(instruction);
    if (opcode != kVectorOpcode && !vector_memory)
        throw std::runtime_error("Invalid vector instruction in RV5 pipeline");

    const uint8_t funct3 = (instruction >> 12) & 0x7u;
    const uint8_t funct6 = (instruction >> 26) & 0x3Fu;
    const uint8_t vd = (instruction >> 7) & 0x1Fu;
    const uint8_t rs1 = (instruction >> 15) & 0x1Fu;
    const uint8_t vs2 = (instruction >> 20) & 0x1Fu;
    const bool vm = ((instruction >> 25) & 1u) != 0;
    const auto read_vector = [&](uint8_t reg)
    {
        if (forwarding_enabled)
        {
            if (ex_mem_reg_.vector_write && ex_mem_reg_.vector_rd == reg)
                return ex_mem_reg_.vector_result;
            if (mem_wb_reg_.prev_vector_write && mem_wb_reg_.prev_vector_rd == reg)
                return mem_wb_reg_.prev_vector_result;
        }
        return registers_.ReadVector(reg);
    };

    if (vector_memory)
    {
        const uint8_t nf = (instruction >> 29) & 0x7u;
        const bool mew = ((instruction >> 28) & 1u) != 0;
        const uint8_t mop = (instruction >> 26) & 0x3u;
        const uint8_t lumop_or_sumop = (instruction >> 20) & 0x1Fu;
        const bool strided = mop == 0b10;
        const bool load = opcode == 0x07u;
        if (nf != 0 || mew || (mop != 0 && !strided) ||
            (!strided && lumop_or_sumop != 0))
            throw std::runtime_error(
                "RV5 supports only unit-stride and strided one-field vector memory operations");
        size_t element_bytes = 0;
        switch (funct3)
        {
        case 0b000: element_bytes = 1; break;
        case 0b101: element_bytes = 2; break;
        case 0b110: element_bytes = 4; break;
        case 0b111: element_bytes = 8; break;
        default: throw std::runtime_error("Unsupported RV5 vector memory element width");
        }
        if (element_bytes * 8u != vector_sew_)
            throw std::runtime_error("RV5 vector memory EEW must match the configured SEW");
        if (!vm && vd == 0)
            throw std::runtime_error("Masked vector memory data/destination register cannot overlap v0");
        execution.vector_memory = true;
        execution.vector_memory_load = load;
        execution.vector_memory_reg = vd;
        execution.vector_memory_element_bytes = static_cast<uint8_t>(element_bytes);
        execution.vector_memory_vl = vector_vl_;
        execution.vector_memory_vm = vm;
        execution.vector_memory_base = forwarding_enabled
            ? forward_gpr(rs1, id_ex_reg_.reg1_data) : id_ex_reg_.reg1_data;
        execution.vector_memory_stride = strided
            ? static_cast<int64_t>(forwarding_enabled
                  ? forward_gpr(vs2, id_ex_reg_.reg2_data) : id_ex_reg_.reg2_data)
            : static_cast<int64_t>(element_bytes);
        execution.vector_memory_data = read_vector(vd);
        execution.vector_memory_mask = read_vector(0);
        execution.vector_write = load;
        execution.vector_rd = vd;
        execution.vector_result = execution.vector_memory_data;
        return execution;
    }

    if (funct3 == 0b111)
    {
        const uint32_t format = instruction >> 30;
        if (format == 0b10)
            throw std::runtime_error("vsetvl is not supported by the RV5 vector subset");
        const bool immediate_avl = format == 0b11;
        const uint16_t zimm = static_cast<uint16_t>(
            (instruction >> 20) & (immediate_avl ? 0x3FFu : 0x7FFu));
        if (zimm != 0 && zimm != 8 && zimm != 16 && zimm != 24)
            throw std::runtime_error(
                "Unsupported vector configuration: RV5 supports LMUL=1, SEW 8/16/32/64");

        const uint8_t new_sew = static_cast<uint8_t>(8u << ((zimm >> 3) & 0x7u));
        const uint8_t vlmax = static_cast<uint8_t>(128u / new_sew);
        uint64_t avl = 0;
        if (immediate_avl)
            avl = rs1;
        else if (rs1 == 0)
        {
            if (vd != 0)
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
            avl = forwarding_enabled ? forward_gpr(rs1, id_ex_reg_.reg1_data)
                                     : id_ex_reg_.reg1_data;

        const uint8_t new_vl = static_cast<uint8_t>(std::min<uint64_t>(avl, vlmax));
        vector_sew_ = new_sew;
        vector_vl_ = new_vl;
        execution.scalar_result = new_vl;
        return execution;
    }

    const bool vector_popcount_mask =
        funct3 == 0b010 && funct6 == 0b010000 && rs1 == 0b10000;

    const auto predicate = read_vector(0);
    const auto lane_is_active = [&](size_t lane)
    {
        return vm || ((predicate[lane / 8u] >> (lane % 8u)) & 1u) != 0;
    };

    // vcpop.m is a mask-register source operation and is allowed to read v0
    // both as its source and as the optional execution predicate.
    if (vector_popcount_mask)
    {
        const auto source_mask = read_vector(vs2);
        uint64_t count = 0;
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            const bool source_bit =
                ((source_mask[lane / 8u] >> (lane % 8u)) & 1u) != 0;
            if (source_bit && lane_is_active(lane))
                ++count;
        }
        execution.scalar_result = count;
        return execution;
    }

    const bool reduction =
        funct3 == 0b010 &&
        (funct6 == 0b000000 || funct6 == 0b000001 || funct6 == 0b000010 ||
         funct6 == 0b000011 || funct6 == 0b000101 || funct6 == 0b000111);
    const bool unary_bit_count =
        funct3 == 0b010 && funct6 == 0b010010 &&
        (rs1 == 0b01100 || rs1 == 0b01101 || rs1 == 0b01110);
    const bool vector_vector_multiply = funct3 == 0b010 && funct6 == 0b100101;
    const bool vector_scalar_multiply = funct3 == 0b110 && funct6 == 0b100101;
    const bool mask_comparison =
        funct3 == 0b000 && funct6 >= 0b011000 && funct6 <= 0b011011;

    if (!vm)
    {
        const bool vs2_is_data =
            funct3 == 0b000 || reduction || unary_bit_count ||
            vector_vector_multiply || vector_scalar_multiply;
        const bool vs1_is_data =
            funct3 == 0b000 || reduction || vector_vector_multiply;
        if ((vs2_is_data && vs2 == 0) || (vs1_is_data && rs1 == 0))
            throw std::runtime_error(
                "Masked vector data sources cannot overlap v0 (mask EEW differs from data EEW)");
        if (vd == 0 && !mask_comparison && !reduction)
            throw std::runtime_error(
                "Masked vector destination cannot overlap v0 unless it writes a mask/reduction");
    }

    if (vector_sew_ != 8 && vector_sew_ != 16 &&
        vector_sew_ != 32 && vector_sew_ != 64)
        throw std::runtime_error("Invalid SEW in RV5 vector state");
    const size_t element_bytes = vector_sew_ / 8u;
    const uint64_t element_mask = vector_sew_ == 64
        ? UINT64_MAX : ((uint64_t{1} << vector_sew_) - 1u);
    if (vector_vl_ > 128u / vector_sew_)
        throw std::runtime_error("VL exceeds VLMAX in RV5 vector state");

    const auto read_element = [element_bytes](const std::array<uint8_t, 16> &bytes,
                                               size_t lane)
    {
        uint64_t value = 0;
        const size_t offset = lane * element_bytes;
        for (size_t byte = 0; byte < element_bytes; ++byte)
            value |= static_cast<uint64_t>(bytes[offset + byte]) << (8u * byte);
        return value;
    };
    const auto write_element = [element_bytes](std::array<uint8_t, 16> &bytes,
                                                size_t lane, uint64_t value)
    {
        const size_t offset = lane * element_bytes;
        for (size_t byte = 0; byte < element_bytes; ++byte)
            bytes[offset + byte] =
                static_cast<uint8_t>((value >> (8u * byte)) & 0xFFu);
    };
    const auto signed_less = [this](uint64_t lhs, uint64_t rhs)
    {
        const uint64_t sign_bit = uint64_t{1} << (vector_sew_ - 1u);
        const bool lhs_negative = (lhs & sign_bit) != 0;
        const bool rhs_negative = (rhs & sign_bit) != 0;
        if (lhs_negative != rhs_negative)
            return lhs_negative;
        return lhs < rhs;
    };

    if (vector_scalar_multiply)
    {
        const auto source = read_vector(vs2);
        auto result = read_vector(vd);
        const uint64_t scalar =
            (forwarding_enabled ? forward_gpr(rs1, id_ex_reg_.reg1_data)
                                : id_ex_reg_.reg1_data) & element_mask;
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            if (!lane_is_active(lane))
                continue;
            write_element(result, lane,
                          (read_element(source, lane) * scalar) & element_mask);
        }
        execution.vector_result = result;
        execution.vector_rd = vd;
        execution.vector_write = true;
        return execution;
    }

    if (vector_vector_multiply)
    {
        const auto lhs = read_vector(vs2);
        const auto rhs = read_vector(rs1);
        auto result = read_vector(vd);
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            if (!lane_is_active(lane))
                continue;
            write_element(result, lane,
                          (read_element(lhs, lane) * read_element(rhs, lane)) & element_mask);
        }
        execution.vector_result = result;
        execution.vector_rd = vd;
        execution.vector_write = true;
        return execution;
    }

    if (unary_bit_count)
    {
        const auto source = read_vector(vs2);
        auto result = read_vector(vd);
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            if (!lane_is_active(lane))
                continue;
            const uint64_t value = read_element(source, lane);
            uint64_t count = 0;
            if (rs1 == 0b01100) // vclz.v
            {
                for (size_t bit = vector_sew_; bit > 0; --bit)
                {
                    if (((value >> (bit - 1u)) & 1u) != 0)
                        break;
                    ++count;
                }
            }
            else if (rs1 == 0b01101) // vctz.v
            {
                for (size_t bit = 0; bit < vector_sew_; ++bit)
                {
                    if (((value >> bit) & 1u) != 0)
                        break;
                    ++count;
                }
            }
            else // vcpop.v
            {
                for (size_t bit = 0; bit < vector_sew_; ++bit)
                    count += (value >> bit) & 1u;
            }
            write_element(result, lane, count);
        }
        execution.vector_result = result;
        execution.vector_rd = vd;
        execution.vector_write = true;
        return execution;
    }

    if (reduction)
    {
        if (vector_vl_ == 0)
            return execution;
        const auto source = read_vector(vs2);
        const auto seed = read_vector(rs1);
        auto result = read_vector(vd);
        uint64_t accumulator = read_element(seed, 0);
        for (size_t lane = 0; lane < vector_vl_; ++lane)
        {
            if (!lane_is_active(lane))
                continue;
            const uint64_t value = read_element(source, lane);
            switch (funct6)
            {
            case 0b000000: accumulator = (accumulator + value) & element_mask; break;
            case 0b000001: accumulator &= value; break;
            case 0b000010: accumulator |= value; break;
            case 0b000011: accumulator ^= value; break;
            case 0b000101: if (signed_less(value, accumulator)) accumulator = value; break;
            case 0b000111: if (signed_less(accumulator, value)) accumulator = value; break;
            default: break;
            }
        }
        write_element(result, 0, accumulator & element_mask);
        execution.vector_result = result;
        execution.vector_rd = vd;
        execution.vector_write = true;
        return execution;
    }

    if (funct3 != 0b000)
        throw std::runtime_error("Unsupported vector operand format in RV5 pipeline subset");

    const bool comparison = funct6 >= 0b011000 && funct6 <= 0b011011;
    const bool arithmetic_or_logic =
        funct6 == 0b000000 || funct6 == 0b000001 || funct6 == 0b000010 ||
        funct6 == 0b001001 || funct6 == 0b001010 || funct6 == 0b001011 ||
        funct6 == 0b010100 || funct6 == 0b010101;
    if (!comparison && !arithmetic_or_logic)
        throw std::runtime_error("Unsupported vector arithmetic/logical instruction in RV5");

    const auto lhs = read_vector(vs2);
    const auto rhs = read_vector(rs1);
    auto result = read_vector(vd);
    for (size_t lane = 0; lane < vector_vl_; ++lane)
    {
        if (!lane_is_active(lane))
            continue;
        const uint64_t a = read_element(lhs, lane);
        const uint64_t b = read_element(rhs, lane);
        if (comparison)
        {
            bool match = false;
            switch (funct6)
            {
            case 0b011000: match = a == b; break;
            case 0b011001: match = a != b; break;
            case 0b011010: match = a < b; break;
            case 0b011011: match = signed_less(a, b); break;
            default: break;
            }
            const uint8_t bit = static_cast<uint8_t>(1u << (lane % 8u));
            if (match)
                result[lane / 8u] |= bit;
            else
                result[lane / 8u] &= static_cast<uint8_t>(~bit);
        }
        else
        {
            uint64_t value = 0;
            switch (funct6)
            {
            case 0b000000: value = (a + b) & element_mask; break; // vadd.vv
            case 0b000001: value = a & (~b) & element_mask; break; // vandn.vv
            case 0b000010: value = (a - b) & element_mask; break; // vsub.vv
            case 0b001001: value = a & b; break; // vand.vv
            case 0b001010: value = a | b; break; // vor.vv
            case 0b001011: value = a ^ b; break; // vxor.vv
            case 0b010100: // vror.vv
            {
                const size_t shift = static_cast<size_t>(b & (vector_sew_ - 1u));
                value = shift == 0 ? a : ((a >> shift) | (a << (vector_sew_ - shift))) & element_mask;
                break;
            }
            case 0b010101: // vrol.vv
            {
                const size_t shift = static_cast<size_t>(b & (vector_sew_ - 1u));
                value = shift == 0 ? a : ((a << shift) | (a >> (vector_sew_ - shift))) & element_mask;
                break;
            }
            default: break;
            }
            write_element(result, lane, value);
        }
    }
    execution.vector_result = result;
    execution.vector_rd = vd;
    execution.vector_write = true;
    return execution;
}

void RV5StageVM_Base::memory_writeback()
{
    auto read_bytes = [&](uint64_t addr, size_t byte_count)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(byte_count);
        for (size_t i = 0; i < byte_count; ++i)
        {
            bytes.push_back(memory_controller_.peekByte(addr + i));
        }
        return bytes;
    };

    switch ((mem_wb_reg_.instruction >> 12) & 0b111)
    {
    case 0b000:
    { // SB
        auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 1);
        memory_controller_.writeByte(ex_mem_reg_.alu_result, ex_mem_reg_.reg2_data & 0xFF);
        auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 1);
        if (old_bytes_vec != new_bytes_vec)
        {
            current_delta_.memory_changes.push_back(
                {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
        }
        break;
    }
    case 0b001:
    { // SH
        auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 2);
        memory_controller_.writeHalfWord(ex_mem_reg_.alu_result, ex_mem_reg_.reg2_data & 0xFFFF);
        auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 2);
        if (old_bytes_vec != new_bytes_vec)
        {
            current_delta_.memory_changes.push_back(
                {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
        }
        break;
    }
    case 0b010:
    { // SW
        auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 4);
        memory_controller_.writeWord(ex_mem_reg_.alu_result, ex_mem_reg_.reg2_data & 0xFFFFFFFF);
        auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 4);
        if (old_bytes_vec != new_bytes_vec)
        {
            current_delta_.memory_changes.push_back(
                {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
        }
        break;
    }
    case 0b011:
    { // SD
        auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 8);
        memory_controller_.writeDoubleWord(ex_mem_reg_.alu_result,
                                           ex_mem_reg_.reg2_data & 0xFFFFFFFFFFFFFFFF);
        auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 8);
        if (old_bytes_vec != new_bytes_vec)
        {
            current_delta_.memory_changes.push_back(
                {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
        }
        break;
    }
    }
}

/** Ignore the next two fucntion they i Ill refactor thosee i promise :'( */

void RV5StageVM_Base::memory_read()
{
    switch ((mem_wb_reg_.instruction >> 12) & 0b111)
    {
    case 0b000:
    { // LB
        mem_wb_reg_.memory_data =
            static_cast<int8_t>(memory_controller_.readByte(ex_mem_reg_.alu_result));
        break;
    }
    case 0b001:
    { // LH
        mem_wb_reg_.memory_data =
            static_cast<int16_t>(memory_controller_.readHalfWord(ex_mem_reg_.alu_result));
        break;
    }
    case 0b010:
    { // LW
        mem_wb_reg_.memory_data =
            static_cast<int32_t>(memory_controller_.readWord(ex_mem_reg_.alu_result));
        break;
    }
    case 0b011:
    { // LD
        mem_wb_reg_.memory_data = memory_controller_.readDoubleWord(ex_mem_reg_.alu_result);
        break;
    }
    case 0b100:
    { // LBU
        mem_wb_reg_.memory_data =
            static_cast<uint8_t>(memory_controller_.readByte(ex_mem_reg_.alu_result));
        break;
    }
    case 0b101:
    { // LHU
        mem_wb_reg_.memory_data =
            static_cast<uint16_t>(memory_controller_.readHalfWord(ex_mem_reg_.alu_result));
        break;
    }
    case 0b110:
    { // LWU
        mem_wb_reg_.memory_data =
            static_cast<uint32_t>(memory_controller_.readWord(ex_mem_reg_.alu_result));
        break;
    }
    }
}

void RV5StageVM_Base::register_write_back(const uint64_t &write_data)
{
    switch (mem_wb_reg_.instruction & 0b1111111)
    {
    case 0b0110011: // R-Type
    case 0b0010011: // I-Type
    case 0b0010111:
    { // AUIPC
        registers_.WriteGpr(mem_wb_reg_.rd, write_data);
        break;
    }
    case 0b0000011:
    { // Load
        registers_.WriteGpr(mem_wb_reg_.rd, write_data);
        break;
    }
    case 0b1100111: // JALR
    case 0b1101111:
    { // JAL
        registers_.WriteGpr(mem_wb_reg_.rd, mem_wb_reg_.pc + 4);
        break;
    }
    case 0b0110111:
    { // LUI
        registers_.WriteGpr(mem_wb_reg_.rd, write_data << 12);
        break;
    }
    default:
        break;
    }
}

//////////////////////

void RV5StageVM_Base::redirect_pc(uint64_t target)
{
    program_counter_ = target;
    pc_redirected_ = true;
}

// IF stage plus PC update. Runs after WB/MEM/EX/ID so a redirect resolved this cycle
// is visible. The target is fetched next cycle, leaving a bubble in IF/ID now, the same
// as hardware that only learns the target at the end of the resolving stage.
void RV5StageVM_Base::fetch_and_advance_pc(bool stalled)
{
    if (pc_redirected_)
    {
        if_id_reg_.reset();
        pc_redirected_ = false;
        return;
    }

    pipeline_fetch();
    // A stall skipped the fetch, so hold the PC on the instruction still to be fetched.
    if (!stalled)
    {
        program_counter_ += 4;
    }
}

void RV5StageVM_Base::pipeline_memory()
{

    // --- B-Type Conditional Branch Resolution (3-Cycle Penalty) ---
    if (ex_mem_reg_.branch_taken && (ex_mem_reg_.instruction & 0b1111111) == 0b1100011)
    {
        // B-Type misprediction confirmed in MEM stage. Hardware flushes the pipeline.

        redirect_pc(ex_mem_reg_.branch_target_pc);
        if_id_reg_.reset();
        id_ex_reg_.reset();
        branch_mispredictions_++;
    }

    // since we are running cycle backward
    // by the time execture check this register forwarding previous results are gone
    // so we store these seperately
    mem_wb_reg_.prev_rd          = mem_wb_reg_.rd;
    mem_wb_reg_.prev_alu_result  = mem_wb_reg_.alu_result;
    mem_wb_reg_.prev_mem_to_reg  = mem_wb_reg_.mem_to_reg;
    mem_wb_reg_.prev_reg_write   = mem_wb_reg_.reg_write;
    mem_wb_reg_.prev_memory_data = mem_wb_reg_.memory_data;
    mem_wb_reg_.prev_vector_result = mem_wb_reg_.vector_result;
    mem_wb_reg_.prev_vector_rd = mem_wb_reg_.vector_rd;
    mem_wb_reg_.prev_vector_write = mem_wb_reg_.vector_write;
    // Save FPR prev fields for forwarding (mirrors GPR prev fields above)
    mem_wb_reg_.prev_frd           = mem_wb_reg_.frd;
    mem_wb_reg_.prev_f_alu_result  = mem_wb_reg_.f_alu_result;
    mem_wb_reg_.prev_f_memory_data = mem_wb_reg_.f_memory_data;
    mem_wb_reg_.prev_freg_write    = mem_wb_reg_.freg_write;
    // --- Standard MEM Operations ---
    mem_wb_reg_.pc          = ex_mem_reg_.pc;
    mem_wb_reg_.instruction = ex_mem_reg_.instruction;
    mem_wb_reg_.alu_result  = ex_mem_reg_.alu_result;
    mem_wb_reg_.rd          = ex_mem_reg_.rd;
    mem_wb_reg_.reg_write   = ex_mem_reg_.reg_write;
    mem_wb_reg_.mem_to_reg  = ex_mem_reg_.mem_to_reg;
    // Latch FPR fields from EX/MEM
    mem_wb_reg_.frd          = ex_mem_reg_.frd;
    mem_wb_reg_.f_alu_result = ex_mem_reg_.f_alu_result;
    mem_wb_reg_.freg_write   = ex_mem_reg_.freg_write;
    mem_wb_reg_.vector_result = ex_mem_reg_.vector_result;
    mem_wb_reg_.vector_rd = ex_mem_reg_.vector_rd;
    mem_wb_reg_.vector_write = ex_mem_reg_.vector_write;

    bool is_F_Instruction = instruction_set::isFInstruction(mem_wb_reg_.instruction);
    bool is_D_Instruction = instruction_set::isDInstruction(mem_wb_reg_.instruction);

    if (ex_mem_reg_.vector_memory)
    {
        const size_t element_bytes = ex_mem_reg_.vector_memory_element_bytes;
        if (element_bytes != 1 && element_bytes != 2 &&
            element_bytes != 4 && element_bytes != 8)
            throw std::runtime_error("Invalid vector memory element width in MEM");
        const uint64_t memory_size = vm_config::config.getMemorySize();
        const auto lane_address = [&](size_t lane) -> uint64_t
        {
            const uint64_t lane_index = static_cast<uint64_t>(lane);
            const int64_t stride = ex_mem_reg_.vector_memory_stride;
            uint64_t address = ex_mem_reg_.vector_memory_base;
            if (stride >= 0)
            {
                const uint64_t magnitude = static_cast<uint64_t>(stride);
                if (lane_index != 0 && magnitude > std::numeric_limits<uint64_t>::max() / lane_index)
                    throw std::out_of_range("Vector memory address overflow");
                const uint64_t offset = magnitude * lane_index;
                if (address > std::numeric_limits<uint64_t>::max() - offset)
                    throw std::out_of_range("Vector memory address overflow");
                address += offset;
            }
            else
            {
                const uint64_t magnitude = static_cast<uint64_t>(-(stride + 1)) + 1u;
                if (lane_index != 0 && magnitude > std::numeric_limits<uint64_t>::max() / lane_index)
                    throw std::out_of_range("Vector memory address underflow");
                const uint64_t offset = magnitude * lane_index;
                if (address < offset)
                    throw std::out_of_range("Vector memory address underflow");
                address -= offset;
            }
            if (address > memory_size || element_bytes > memory_size - address)
                throw std::out_of_range("Active vector memory lane is out of range");
            return address;
        };
        const auto lane_is_active = [&](size_t lane)
        {
            return ex_mem_reg_.vector_memory_vm ||
                   ((ex_mem_reg_.vector_memory_mask[lane / 8u] >> (lane % 8u)) & 1u) != 0;
        };
        struct PendingVectorLane
        {
            size_t lane;
            uint64_t address;
            std::vector<uint8_t> old_bytes;
            std::vector<uint8_t> new_bytes;
        };
        std::vector<PendingVectorLane> pending;
        pending.reserve(ex_mem_reg_.vector_memory_vl);
        for (size_t lane = 0; lane < ex_mem_reg_.vector_memory_vl; ++lane)
        {
            if (!lane_is_active(lane))
                continue;
            PendingVectorLane item{lane, lane_address(lane), {}, {}};
            item.new_bytes.reserve(element_bytes);
            if (ex_mem_reg_.vector_memory_load)
            {
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    item.new_bytes.push_back(memory_controller_.readByte(item.address + byte));
            }
            else
            {
                item.old_bytes.reserve(element_bytes);
                for (size_t byte = 0; byte < element_bytes; ++byte)
                {
                    item.old_bytes.push_back(memory_controller_.peekByte(item.address + byte));
                    item.new_bytes.push_back(
                        ex_mem_reg_.vector_memory_data[lane * element_bytes + byte]);
                }
            }
            pending.push_back(std::move(item));
        }
        if (ex_mem_reg_.vector_memory_load)
        {
            auto loaded = ex_mem_reg_.vector_memory_data;
            for (const auto &item : pending)
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    loaded[item.lane * element_bytes + byte] = item.new_bytes[byte];
            mem_wb_reg_.vector_rd = ex_mem_reg_.vector_memory_reg;
            mem_wb_reg_.vector_result = loaded;
            mem_wb_reg_.vector_write = true;
        }
        else
        {
            for (const auto &item : pending)
            {
                for (size_t byte = 0; byte < element_bytes; ++byte)
                    memory_controller_.writeByte(item.address + byte, item.new_bytes[byte]);
                if (item.old_bytes != item.new_bytes)
                    current_delta_.memory_changes.push_back(
                        {item.address, item.old_bytes, item.new_bytes});
            }
        }
        return;
    }

    // Memory Access
    if (ex_mem_reg_.mem_read)
    {
        // Load instruction: Result available at end of this stage (Load-Use still needs 1 NOP
        // stall)
        if (is_F_Instruction)
        { // FLW

            mem_wb_reg_.memory_data =
                static_cast<int32_t>(memory_controller_.readWord(ex_mem_reg_.alu_result));
            mem_wb_reg_.f_memory_data = mem_wb_reg_.memory_data;
        }
        else if (is_D_Instruction)
        { // FLD
            mem_wb_reg_.memory_data = memory_controller_.readDoubleWord(ex_mem_reg_.alu_result);
            mem_wb_reg_.f_memory_data = mem_wb_reg_.memory_data;
        }
        else
        {
            switch ((mem_wb_reg_.instruction >> 12) & 0b111)
            {
            case 0b000:
            { // LB
                mem_wb_reg_.memory_data =
                    static_cast<int8_t>(memory_controller_.readByte(ex_mem_reg_.alu_result));
                break;
            }
            case 0b001:
            { // LH
                mem_wb_reg_.memory_data =
                    static_cast<int16_t>(memory_controller_.readHalfWord(ex_mem_reg_.alu_result));
                break;
            }
            case 0b010:
            { // LW
                mem_wb_reg_.memory_data =
                    static_cast<int32_t>(memory_controller_.readWord(ex_mem_reg_.alu_result));
                break;
            }
            case 0b011:
            { // LD
                mem_wb_reg_.memory_data = memory_controller_.readDoubleWord(ex_mem_reg_.alu_result);
                break;
            }
            case 0b100:
            { // LBU
                mem_wb_reg_.memory_data =
                    static_cast<uint8_t>(memory_controller_.readByte(ex_mem_reg_.alu_result));
                break;
            }
            case 0b101:
            { // LHU
                mem_wb_reg_.memory_data =
                    static_cast<uint16_t>(memory_controller_.readHalfWord(ex_mem_reg_.alu_result));
                break;
            }
            case 0b110:
            { // LWU
                mem_wb_reg_.memory_data =
                    static_cast<uint32_t>(memory_controller_.readWord(ex_mem_reg_.alu_result));
                break;
            }
            }
        }
    }
    else if (ex_mem_reg_.mem_write)
    {
        auto read_bytes = [&](uint64_t addr, size_t byte_count)
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(byte_count);
            for (size_t i = 0; i < byte_count; ++i)
            {
                bytes.push_back(memory_controller_.peekByte(addr + i));
            }
            return bytes;
        };

        if (is_F_Instruction)
        { // FSW
            auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 4);
            memory_controller_.writeWord(ex_mem_reg_.alu_result,
                                         ex_mem_reg_.freg2_data & 0xFFFFFFFF);
            auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 4);
            if (old_bytes_vec != new_bytes_vec)
            {
                current_delta_.memory_changes.push_back(
                    {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
            }
        }
        else if (is_D_Instruction)
        { // FSD
            auto old_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 8);
            memory_controller_.writeDoubleWord(ex_mem_reg_.alu_result,
                                               ex_mem_reg_.freg2_data & 0xFFFFFFFFFFFFFFFF);
            auto new_bytes_vec = read_bytes(ex_mem_reg_.alu_result, 8);
            if (old_bytes_vec != new_bytes_vec)
            {
                current_delta_.memory_changes.push_back(
                    {ex_mem_reg_.alu_result, old_bytes_vec, new_bytes_vec});
            }
        }
        else
        {
            memory_writeback();
        }
    }
}

void RV5StageVM_Base::pipeline_writeback()
{
    // Every real instruction reaching WB retires, whether or not it writes a register
    // (stores, branches, jumps to x0, nops). Bubbles carry INVALID_PC.
    if (mem_wb_reg_.pc != INVALID_PC)
    {
        instructions_retired_++;
    }

    if (mem_wb_reg_.vector_write)
    {
        const auto old_value = registers_.ReadVector(mem_wb_reg_.vector_rd);
        if (old_value != mem_wb_reg_.vector_result)
        {
            RegisterChange change{mem_wb_reg_.vector_rd, 3, 0, 0};
            change.old_vector_value = old_value;
            change.new_vector_value = mem_wb_reg_.vector_result;
            current_delta_.register_changes.push_back(change);
        }
        registers_.WriteVector(mem_wb_reg_.vector_rd, mem_wb_reg_.vector_result);
    }

    if (!mem_wb_reg_.reg_write && !mem_wb_reg_.freg_write)
    {
        return;
    }

    // --- FP Writeback Delegation ---
    // Before the rd != 0 check: for FP ops rd names an FPR, and f0 is writable.
    if (instruction_set::isFInstruction(mem_wb_reg_.instruction))
    {
        pipeline_writeback_float();
        return;
    }
    else if (instruction_set::isDInstruction(mem_wb_reg_.instruction))
    {
        pipeline_writeback_double();
        return;
    }

    if (mem_wb_reg_.rd != 0)
    {
        uint64_t write_data =
            mem_wb_reg_.mem_to_reg ? mem_wb_reg_.memory_data : mem_wb_reg_.alu_result;
        // JAL/JALR write the link address, not the ALU result.
        uint8_t opcode = mem_wb_reg_.instruction & 0b1111111;
        if (opcode == 0b1100111 || opcode == 0b1101111)
        {
            write_data = mem_wb_reg_.pc + 4;
        }

        // Record state for Undo/Redo
        uint64_t old_value = registers_.ReadGpr(mem_wb_reg_.rd);
        if (old_value != write_data)
        {
            current_delta_.register_changes.push_back({mem_wb_reg_.rd,
                                                       0, // GPR type
                                                       old_value, write_data});
        }

        switch (opcode)
        {
        case 0b0110011: // R-Type
        case 0b0010011: // I-Type
        case 0b0010111: // AUIPC
        case 0b0111011: // OP-32 R-type (ADDW, SUBW, MULW, DIVW, DIVUW, REMW, REMUW, SLLW, SRLW, SRAW)
        case 0b0011011: // OP-32 I-type (ADDIW, SLLIW, SRLIW, SRAIW)
        {
            registers_.WriteGpr(mem_wb_reg_.rd, write_data);
            break;
        }
        case 0b0000011:
        { // Load
            registers_.WriteGpr(mem_wb_reg_.rd, write_data);
            break;
        }
        case 0b1100111: // JALR
        case 0b1101111:
        { // JAL
            registers_.WriteGpr(mem_wb_reg_.rd, mem_wb_reg_.pc + 4);
            break;
        }
        case 0b0110111:
        { // LUI
            registers_.WriteGpr(mem_wb_reg_.rd, write_data);
            break;
        }
        case 0b1010111: // Vector configuration and vcpop.m return a scalar in rd.
        {
            const uint8_t funct3 = (mem_wb_reg_.instruction >> 12) & 0x7u;
            const uint8_t funct6 = (mem_wb_reg_.instruction >> 26) & 0x3Fu;
            const uint8_t vs1 = (mem_wb_reg_.instruction >> 15) & 0x1Fu;
            if (funct3 == 0b111 ||
                (funct3 == 0b010 && funct6 == 0b010000 && vs1 == 0b10000))
                registers_.WriteGpr(mem_wb_reg_.rd, write_data);
            break;
        }
        default:
            break;
        }
    }
}

void RV5StageVM_Base::pipeline_writeback_float()
{
    // yes i just copy pasted this from rvss
    uint64_t current_instruction_ = mem_wb_reg_.instruction;
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    // write to GPR
    if (funct7 == 0b1010000 || funct7 == 0b1100000 || funct7 == 0b1110000)
    { // f(eq|lt|le).s, fcvt.(w|wu|l|lu).s
        old_reg = registers_.ReadGpr(rd);
        registers_.WriteGpr(rd, mem_wb_reg_.alu_result);
        new_reg = mem_wb_reg_.alu_result;
        reg_type = 0; // GPR
    }
    // write to FPR
    else if (opcode == 0b0000111)
    {
        old_reg = registers_.ReadFpr(rd);
        registers_.WriteFpr(rd, mem_wb_reg_.memory_data);
        new_reg = mem_wb_reg_.memory_data;
        reg_type = 2; // FPR
    }
    else
    {
        old_reg = registers_.ReadFpr(rd);
        registers_.WriteFpr(rd, mem_wb_reg_.alu_result);
        new_reg = mem_wb_reg_.alu_result;
        reg_type = 2; // FPR
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

void RV5StageVM_Base::pipeline_writeback_double()
{
    uint64_t current_instruction_ = mem_wb_reg_.instruction;
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    // write to GPR
    if (funct7 == 0b1010001 || funct7 == 0b1100001 || funct7 == 0b1110001)
    { // f(eq|lt|le).d, fcvt.(w|wu|l|lu).d
        old_reg = registers_.ReadGpr(rd);
        registers_.WriteGpr(rd, mem_wb_reg_.alu_result);
        new_reg = mem_wb_reg_.alu_result;
        reg_type = 0; // GPR
    }
    // write to FPR
    else if (opcode == 0b0000111)
    {
        old_reg = registers_.ReadFpr(rd);
        registers_.WriteFpr(rd, mem_wb_reg_.memory_data);
        new_reg = mem_wb_reg_.memory_data;
        reg_type = 2; // FPR
    }
    else
    {
        old_reg = registers_.ReadFpr(rd);
        registers_.WriteFpr(rd, mem_wb_reg_.alu_result);
        new_reg = mem_wb_reg_.alu_result;
        reg_type = 2; // FPR
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

uint64_t RV5StageVM_Base::execute_float()
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

    if (funct7 == 0b1101000 || funct7 == 0b1111000 || opcode == 0b0000111 || opcode == 0b0100111)
    {
        reg1_value = registers_.ReadGpr(id_ex_reg_.rs1);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    alu::AluOp aluOperation = control_unit_.GetAluSignal(instruction, id_ex_reg_.alu_op > 0);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    registers_.WriteCsr(0x003, fcsr_status);
    return alu_result;
}

uint64_t RV5StageVM_Base::execute_double()
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

    if (funct7 == 0b1101001 || funct7 == 0b1111001 || opcode == 0b0000111 || opcode == 0b0100111)
    {
        reg1_value = registers_.ReadGpr(id_ex_reg_.rs1);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    alu::AluOp alu_operation = control_unit_.GetAluSignal(instruction, id_ex_reg_.alu_op > 0);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::dfpexecute(alu_operation, reg1_value, reg2_value, reg3_value, rm);

    registers_.WriteCsr(0x003, fcsr_status);
    return alu_result;
}

bool RV5StageVM_Base::is_pipeline_drained() const
{
    // A stage holds a real instruction (possibly an actual `nop`) unless its PC is
    // INVALID_PC, which is what flushes, stalls and fetches past the end leave behind.
    if (if_id_reg_.pc != INVALID_PC || id_ex_reg_.pc != INVALID_PC ||
        ex_mem_reg_.pc != INVALID_PC || mem_wb_reg_.pc != INVALID_PC)
        return false;
    if (if_id_reg_.instruction != NOP)
        return false;
    if (id_ex_reg_.instruction != NOP)
        return false;
    if (ex_mem_reg_.instruction != NOP)
        return false;
    if (mem_wb_reg_.instruction != NOP)
        return false;
    // EX/MEM: Check for architectural side effects (Reg Write, Mem Read, Mem Write).
    // A NOP will have all these control signals disabled (false).
    if (ex_mem_reg_.reg_write || ex_mem_reg_.freg_write || ex_mem_reg_.mem_read ||
        ex_mem_reg_.mem_write || ex_mem_reg_.vector_write)
        return false;

    // MEM/WB: Check for final architectural side effect (GPR or FPR write).
    if (mem_wb_reg_.reg_write || mem_wb_reg_.freg_write || mem_wb_reg_.vector_write)
        return false;

    return true;
}

void RV5StageVM_Base::Undo()
{
    if (undo_stack_.empty())
    {
        return;
    }

    RV5StageStepDelta last = undo_stack_.top();
    undo_stack_.pop();

    for (auto it = last.register_changes.rbegin(); it != last.register_changes.rend(); ++it)
    {
        const auto &change = *it;
        switch (change.reg_type)
        {
        case 0:
            registers_.WriteGpr(change.reg_index, change.old_value);
            break;
        case 1:
            registers_.WriteCsr(change.reg_index, change.old_value);
            break;
        case 2:
            registers_.WriteFpr(change.reg_index, change.old_value);
            break;
        case 3:
            registers_.WriteVector(change.reg_index, change.old_vector_value);
            break;
        default:
            break;
        }
    }

    for (auto it = last.memory_changes.rbegin(); it != last.memory_changes.rend(); ++it)
    {
        const auto &change = *it;
        for (size_t i = 0; i < change.old_bytes_vec.size(); ++i)
        {
            // Through the cache, like the store being undone: bypassing it would leave the
            // new value in a dirty L1 line, where every later read still sees it.
            memory_controller_.writeByte(change.address + i, change.old_bytes_vec[i]);
        }
    }

    program_counter_ = last.old_pc;
    cycle_s_ = last.old_cycle;
    instructions_retired_ = last.old_instructions_retired;
    stall_cycles_ = last.old_stall_cycles;
    branch_mispredictions_ = last.old_branch_mispredictions;
    vector_sew_ = last.old_vector_sew;
    vector_vl_ = last.old_vector_vl;

    if_id_reg_ = last.pipeline_register_change.old_if_id_reg;
    id_ex_reg_ = last.pipeline_register_change.old_id_ex_reg;
    ex_mem_reg_ = last.pipeline_register_change.old_ex_mem_reg;
    mem_wb_reg_ = last.pipeline_register_change.old_mem_wb_reg;

    redo_stack_.push(last);

    cpi_ = instructions_retired_
               ? static_cast<float>(cycle_s_) / static_cast<float>(instructions_retired_)
               : 0.0f;
    ipc_ =
        cycle_s_ ? static_cast<float>(instructions_retired_) / static_cast<float>(cycle_s_) : 0.0f;
    setProcessorState();
    SetActiveWireNames();
    emit updateCircuitStateSignal(active_wires_);
    emit processorClockedSignal(processor_state_);
}

void RV5StageVM_Base::Redo()
{
    if (redo_stack_.empty())
    {
        return;
    }

    RV5StageStepDelta next = redo_stack_.top();
    redo_stack_.pop();

    for (const auto &change : next.register_changes)
    {
        switch (change.reg_type)
        {
        case 0:
            registers_.WriteGpr(change.reg_index, change.new_value);
            break;
        case 1:
            registers_.WriteCsr(change.reg_index, change.new_value);
            break;
        case 2:
            registers_.WriteFpr(change.reg_index, change.new_value);
            break;
        case 3:
            registers_.WriteVector(change.reg_index, change.new_vector_value);
            break;
        default:
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
    cycle_s_ = next.new_cycle;
    instructions_retired_ = next.new_instructions_retired;
    stall_cycles_ = next.new_stall_cycles;
    branch_mispredictions_ = next.new_branch_mispredictions;
    vector_sew_ = next.new_vector_sew;
    vector_vl_ = next.new_vector_vl;

    if_id_reg_ = next.pipeline_register_change.new_if_id_reg;
    id_ex_reg_ = next.pipeline_register_change.new_id_ex_reg;
    ex_mem_reg_ = next.pipeline_register_change.new_ex_mem_reg;
    mem_wb_reg_ = next.pipeline_register_change.new_mem_wb_reg;

    undo_stack_.push(next);

    cpi_ = instructions_retired_
               ? static_cast<float>(cycle_s_) / static_cast<float>(instructions_retired_)
               : 0.0f;
    ipc_ =
        cycle_s_ ? static_cast<float>(instructions_retired_) / static_cast<float>(cycle_s_) : 0.0f;
    setProcessorState();
    SetActiveWireNames();
    emit updateCircuitStateSignal(active_wires_);
    emit processorClockedSignal(processor_state_);
}

void RV5StageVM_Base::print_pipeline_registers_debug()
{
    std::cout << "\xEF\xBB\xBF"; // UTF-8 BOM (optional but helps old terminals)

    std::cout << "\n┌──────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Pipeline Debug (Cycle " << cycle_s_ << ")"
              << std::string(34 - std::to_string(cycle_s_).size(), ' ') << "│\n";
    std::cout << "├──────────────────────────────────────────────────────────┤\n";

    std::cout << "│ PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << program_counter_
              << std::dec << "                                           │\n";

    auto inst_to_mnemonic = [](uint32_t inst) -> const char *
    {
        if (inst == NOP)
            return "NOP";
        uint8_t opc = inst & 0x7F;
        switch (opc)
        {
        case 0b1101111:
            return "JAL";
        case 0b1100111:
            return "JALR";
        case 0b1100011:
            return "BR";
        case 0b0000011:
            return "LOAD";
        case 0b0100011:
            return "STORE";
        case 0b0010011:
            return "ALU_IMM";
        case 0b0110011:
            return "ALU_REG";
        case 0b1110011:
            return "SYSTEM";
        default:
            return "OTHER";
        }
    };

    auto print_row = [&](const char *stage, uint32_t inst)
    {
        std::stringstream ss;
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << inst << std::dec;

        std::cout << "│ " << std::left << std::setw(6) << stage << "│ " << std::setw(12) << ss.str()
                  << "│ " << std::setw(10) << inst_to_mnemonic(inst) << "│\n";
    };

    std::cout << "├──────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Stage  │ Instruction  │ Mnemonic                         │\n";
    std::cout << "├──────────────────────────────────────────────────────────┤\n";

    print_row("IF/ID", if_id_reg_.instruction);
    print_row("ID/EX", id_ex_reg_.instruction);
    print_row("EX/MEM", ex_mem_reg_.instruction);
    print_row("MEM/WB", mem_wb_reg_.instruction);

    // You had not printed MEM/WB, so I didn’t add it
    // Same logic preserved exactly

    std::cout << "└──────────────────────────────────────────────────────────┘\n";
}
}//namespace Kites
