/**
 * @file code_generator.cpp
 * @brief Contains the implementation of functions for generating machine code from intermediate
 * code.
 * @author Vishank Singh, https://github.com/VishankSingh
 */

#include "code_generator.h"
#include "common/instructions.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace Kites
{
std::vector<std::string>
printIntermediateCode(const std::vector<std::pair<ICUnit, bool>> &IntermediateCode)
{
    std::vector<std::string> ICList;
    for (const auto &pair : IntermediateCode)
    {
        const ICUnit &block = pair.first;
        std::string code;

        if (instruction_set::isValidVectorInstruction(block.getOpcode()))
        {
            if (block.getOpcode() == "vsetvli")
                code = block.getOpcode() + " " + block.getRd() + ", " + block.getRs1() + ", " + block.getImm();
            else if (block.getOpcode() == "vcpop.m")
                code = block.getOpcode() + " " + block.getRd() + ", " + block.getRs1();
            else if (block.getOpcode() == "vclz.v" || block.getOpcode() == "vctz.v" ||
                     block.getOpcode() == "vcpop.v")
                code = block.getOpcode() + " " + block.getRd() + ", " + block.getRs1();
            else if (block.getOpcode().rfind("vlse", 0) == 0 ||
                     block.getOpcode().rfind("vsse", 0) == 0)
                code = block.getOpcode() + " " + block.getRd() + ", (" + block.getRs1() +
                       "), " + block.getRs2();
            else if (block.getOpcode().rfind("vle", 0) == 0 ||
                     block.getOpcode().rfind("vse", 0) == 0)
                code = block.getOpcode() + " " + block.getRd() + ", (" + block.getRs1() + ")";
            else
                code = block.getOpcode() + " " + block.getRd() + ", " + block.getRs1() + ", " + block.getRs2();
            if (block.getImm() == "0" && block.getOpcode() != "vsetvli")
                code += ", v0.t";
        }
        else if (instruction_set::isValidRTypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRd() + " " + block.getRs1() + " " +
                   block.getRs2();
        }
        else if (instruction_set::isValidITypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRd() + " " + block.getRs1() + " " +
                   block.getImm();
        }
        else if (instruction_set::isValidSTypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRs2() + " " + block.getImm() + "(" +
                   block.getRs1() + ")";
        }
        else if (instruction_set::isValidBTypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRs1() + " " + block.getRs2() + " " +
                   block.getImm() + " <" + block.getLabel() + ">";
        }
        else if (instruction_set::isValidUTypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRd() + " " + block.getImm();
        }
        else if (instruction_set::isValidJTypeInstruction(block.getOpcode()))
        {
            code = block.getOpcode() + " " + block.getRd() + " " + block.getImm() + " <" +
                   block.getLabel() + ">";
        }
        else
        {
            code = block.getOpcode() + " " + block.getImm();
        }

        ICList.push_back(code);
    }
    return ICList;
}

static inline uint32_t extractRegisterIndex(const std::string &reg)
{
    return static_cast<uint32_t>(std::stoi(reg.substr(1)));
}

uint32_t generateRTypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::R_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    const uint32_t funct3 = encoding.funct3.to_ulong();
    const uint32_t funct7 = encoding.funct7.to_ulong();
    const uint32_t opcode = encoding.opcode.to_ulong();
    uint32_t machineCode = 0;
    machineCode |= (funct7 << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (funct3 << 12);
    machineCode |= (rd << 7);
    machineCode |= opcode;
    return machineCode;
}

uint32_t generateI1TypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::I1_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t imm = static_cast<uint32_t>(std::stoi(block.getImm()));
    const uint32_t funct3 = encoding.funct3.to_ulong();
    const uint32_t opcode = encoding.opcode.to_ulong();
    uint32_t machineCode = 0;
    machineCode |= (imm << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (funct3 << 12);
    machineCode |= (rd << 7);
    machineCode |= opcode;
    return machineCode;
}

uint32_t generateI2TypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::I2_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t imm = static_cast<uint32_t>(std::stoi(block.getImm()));
    uint32_t machineCode = 0;
    machineCode |= (encoding.funct6.to_ulong() << 26);
    machineCode |= (imm << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateI3TypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::I3_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = 0;
    const uint32_t rs1 = 0;
    const uint32_t imm = static_cast<uint32_t>(encoding.imm.to_ulong());
    uint32_t machineCode = 0;
    machineCode |= (imm << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateSTypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::S_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    const uint32_t imm = static_cast<uint32_t>(std::stoi(block.getImm()));
    const uint32_t imm_lo = imm & 0b11111;          // bits [4:0]
    const uint32_t imm_hi = (imm >> 5) & 0b1111111; // bits [11:5]
    uint32_t machineCode = 0;
    machineCode |= (imm_hi << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (imm_lo << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateBTypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::B_type_instruction_encoding_map.at(block.getOpcode());
    uint32_t rs1 = extractRegisterIndex(block.getRs1());
    uint32_t rs2 = extractRegisterIndex(block.getRs2());
    int32_t imm = std::stoi(block.getImm());
    uint32_t imm12 = (imm >> 12) & 0b1;
    uint32_t imm10_5 = (imm >> 5) & 0b111111;
    uint32_t imm4_1 = (imm >> 1) & 0b1111;
    uint32_t imm11 = (imm >> 11) & 0b1;
    uint32_t machineCode = 0;
    machineCode |= (imm12 << 31);
    machineCode |= (imm10_5 << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (imm4_1 << 8);
    machineCode |= (imm11 << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateUTypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::U_type_instruction_encoding_map.at(block.getOpcode());
    uint32_t rd = extractRegisterIndex(block.getRd());
    uint32_t imm =
        static_cast<uint32_t>(std::stoi(block.getImm())) & 0xFFFFF; // U-type: top 20 bits
    uint32_t machineCode = 0;
    machineCode |= (imm << 12);                // bits [31:12]
    machineCode |= (rd << 7);                  // bits [11:7]
    machineCode |= encoding.opcode.to_ulong(); // bits [6:0]
    return machineCode;
}

uint32_t generateJTypeMachineCode(const ICUnit &block)
{
    const auto &encoding = instruction_set::J_type_instruction_encoding_map.at(block.getOpcode());
    uint32_t rd = extractRegisterIndex(block.getRd());
    int32_t imm = static_cast<int32_t>(std::stoi(block.getImm()));
    uint32_t machineCode = 0;
    machineCode |= ((imm & 0x100000) << 11);   // imm[20] to bit 31
    machineCode |= ((imm & 0x7FE) << 20);      // imm[10:1] to bits 30:21
    machineCode |= ((imm & 0x800) << 9);       // imm[11] to bit 20
    machineCode |= ((imm & 0xFF000) << 0);     // imm[19:12] to bits 19:12
    machineCode |= (rd << 7);                  // bits 11:7
    machineCode |= encoding.opcode.to_ulong(); // bits 6:0
    return machineCode;
}

uint32_t generateCSRRTypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::CSR_R_type_instruction_encoding_map.at(block.getOpcode());
    uint32_t rd = extractRegisterIndex(block.getRd());
    uint32_t rs1 = extractRegisterIndex(block.getRs1());
    uint32_t csr = static_cast<uint32_t>(block.getCsr()) & 0xFFF; // CSR is 12-bit
    uint32_t machineCode = 0;
    machineCode |= (csr << 20);                        // csr[31:20]
    machineCode |= (rs1 << 15);                        // rs1[19:15]
    machineCode |= (encoding.funct3.to_ulong() << 12); // funct3[14:12]
    machineCode |= (rd << 7);                          // rd[11:7]
    machineCode |= encoding.opcode.to_ulong();         // opcode[6:0]
    return machineCode;
}

uint32_t generateCSRITypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::CSR_I_type_instruction_encoding_map.at(block.getOpcode());
    uint32_t rd = extractRegisterIndex(block.getRd());
    uint32_t zimm =
        static_cast<uint32_t>(std::stoi(block.getImm())) & 0b11111; // zimm is 5-bit (not 3-bit!)
    uint32_t csr = static_cast<uint32_t>(block.getCsr()) & 0xFFF;   // csr is 12-bit
    uint32_t machineCode = 0;
    machineCode |= (csr << 20);                        // csr[31:20]
    machineCode |= (zimm << 15);                       // zimm[19:15] (not rs1)
    machineCode |= (encoding.funct3.to_ulong() << 12); // funct3[14:12]
    machineCode |= (rd << 7);                          // rd[11:7]
    machineCode |= encoding.opcode.to_ulong();         // opcode[6:0]
    return machineCode;
}

uint32_t generateFDRTypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_R_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    uint32_t machineCode = 0;
    machineCode |= (encoding.funct7.to_ulong() << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDR1TypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_R1_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    const uint32_t rm = static_cast<uint32_t>(block.getRm() & 0b111);
    uint32_t machineCode = 0;
    machineCode |= (encoding.funct7.to_ulong() << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (rm << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDR2TypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_R2_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rm = static_cast<uint32_t>(block.getRm() & 0b111);
    uint32_t machineCode = 0;
    machineCode |= (encoding.funct7.to_ulong() << 25);
    machineCode |= (encoding.funct5.to_ulong() << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (rm << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDR3TypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_R3_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    uint32_t machineCode = 0;
    machineCode |= (encoding.funct7.to_ulong() << 25);
    machineCode |= (encoding.funct5.to_ulong() << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDR4TypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_R4_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    const uint32_t rs3 = extractRegisterIndex(block.getRs3());
    const uint32_t rm = static_cast<uint32_t>(block.getRm() & 0b111);
    uint32_t machineCode = 0;
    machineCode |= (rs3 << 27);
    machineCode |= (encoding.funct2.to_ulong() << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (rm << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDITypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_I_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rd = extractRegisterIndex(block.getRd());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t imm = static_cast<uint32_t>(std::stoi(block.getImm()));
    uint32_t machineCode = 0;
    machineCode |= (imm << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (rd << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

uint32_t generateFDSTypeMachineCode(const ICUnit &block)
{
    const auto &encoding =
        instruction_set::F_D_S_type_instruction_encoding_map.at(block.getOpcode());
    const uint32_t rs1 = extractRegisterIndex(block.getRs1());
    const uint32_t rs2 = extractRegisterIndex(block.getRs2());
    const uint32_t imm = static_cast<uint32_t>(std::stoi(block.getImm()));
    const uint32_t imm_lo = imm & 0b11111;          // bits [4:0]
    const uint32_t imm_hi = (imm >> 5) & 0b1111111; // bits [11:5]
    uint32_t machineCode = 0;
    machineCode |= (imm_hi << 25);
    machineCode |= (rs2 << 20);
    machineCode |= (rs1 << 15);
    machineCode |= (encoding.funct3.to_ulong() << 12);
    machineCode |= (imm_lo << 7);
    machineCode |= encoding.opcode.to_ulong();
    return machineCode;
}

std::vector<uint32_t>
generateMachineCode(const std::vector<std::pair<ICUnit, bool>> &IntermediateCode)
{
    std::vector<uint32_t> machine_code;
    for (const auto &pair : IntermediateCode)
    {
        const ICUnit &block = pair.first;
        uint32_t code;
        if (instruction_set::isValidVectorInstruction(block.getOpcode()))
        {
            const uint32_t rd = static_cast<uint32_t>(std::stoi(block.getRd().substr(1)));
            const uint32_t rs1 = static_cast<uint32_t>(std::stoi(block.getRs1().substr(1)));
            if (block.getOpcode() == "vsetvli")
            {
                const uint32_t zimm = static_cast<uint32_t>(std::stoul(block.getImm()));
                code = (zimm << 20) | (rs1 << 15) | (0b111u << 12) | (rd << 7) | 0x57u;
            }
            else if (block.getOpcode() == "vcpop.m")
            {
                const uint32_t vs2 = rs1;
                const uint32_t vm = block.getImm() == "0" ? 0u : 1u;
                code = (0b010000u << 26) | (vm << 25) | (vs2 << 20) |
                       (0b10000u << 15) | (0b110u << 12) | (rd << 7) | 0x57u;
            }
            else if (block.getOpcode() == "vclz.v" || block.getOpcode() == "vctz.v" ||
                     block.getOpcode() == "vcpop.v")
            {
                uint32_t unary_vs1 = 0;
                if (block.getOpcode() == "vclz.v") unary_vs1 = 0b01100;
                else if (block.getOpcode() == "vctz.v") unary_vs1 = 0b01101;
                else unary_vs1 = 0b01110;
                const uint32_t vm = block.getImm() == "0" ? 0u : 1u;
                code = (0b010010u << 26) | (vm << 25) | (rs1 << 20) |
                       (unary_vs1 << 15) | (0b010u << 12) | (rd << 7) | 0x57u;
            }
            else if (block.getOpcode().rfind("vlse", 0) == 0 ||
                     block.getOpcode().rfind("vsse", 0) == 0 ||
                     block.getOpcode().rfind("vle", 0) == 0 ||
                     block.getOpcode().rfind("vse", 0) == 0)
            {
                const bool is_strided = block.getOpcode().rfind("vlse", 0) == 0 ||
                                        block.getOpcode().rfind("vsse", 0) == 0;
                const size_t dot = block.getOpcode().find('.');
                const size_t prefix_length = is_strided ? 4u : 3u;
                const uint32_t element_width =
                    static_cast<uint32_t>(std::stoul(block.getOpcode().substr(
                        prefix_length, dot - prefix_length)));
                uint32_t width_encoding = 0;
                switch (element_width)
                {
                case 8: width_encoding = 0b000; break;
                case 16: width_encoding = 0b101; break;
                case 32: width_encoding = 0b110; break;
                case 64: width_encoding = 0b111; break;
                default:
                    throw std::runtime_error("Unsupported vector memory element width");
                }
                const bool is_load = block.getOpcode().rfind("vle", 0) == 0 ||
                                     block.getOpcode().rfind("vlse", 0) == 0;
                const uint32_t opcode = is_load ? 0x07u : 0x27u;
                const uint32_t mop = is_strided ? 0b10u : 0u;
                const uint32_t stride_register = is_strided
                    ? static_cast<uint32_t>(std::stoul(block.getRs2().substr(1)))
                    : 0u;
                // mop=00 is unit stride; mop=10 is strided. vm=0 means v0.t.
                const uint32_t vm = block.getImm() == "0" ? 0u : 1u;
                code = (mop << 26) | (vm << 25) | (stride_register << 20) |
                       (rs1 << 15) | (width_encoding << 12) | (rd << 7) | opcode;
            }
            else
            {
                // Assembly syntax is vd, vs2, vs1. The encoded instruction
                // stores vs2 in bits [24:20] and vs1 in bits [19:15].
                const uint32_t vs1 = static_cast<uint32_t>(std::stoi(block.getRs2().substr(1)));
                const uint32_t vs2 = rs1;
                uint32_t funct6 = 0;
                uint32_t funct3 = 0b000;
                const std::string &mnemonic = block.getOpcode();
                if (mnemonic == "vredsum.vs") { funct6 = 0b000000; funct3 = 0b010; }
                else if (mnemonic == "vredand.vs") { funct6 = 0b000001; funct3 = 0b010; }
                else if (mnemonic == "vredor.vs") { funct6 = 0b000010; funct3 = 0b010; }
                else if (mnemonic == "vredxor.vs") { funct6 = 0b000011; funct3 = 0b010; }
                else if (mnemonic == "vredmin.vs") { funct6 = 0b000101; funct3 = 0b010; }
                else if (mnemonic == "vredmax.vs") { funct6 = 0b000111; funct3 = 0b010; }
                else if (mnemonic == "vadd.vv") funct6 = 0b000000;
                else if (mnemonic == "vmul.vv") funct6 = 0b100101;
                else if (mnemonic == "vmul.vx") { funct6 = 0b100101; funct3 = 0b100; }
                else if (mnemonic == "vsub.vv") funct6 = 0b000010;
                else if (mnemonic == "vandn.vv") funct6 = 0b000001;
                else if (mnemonic == "vand.vv") funct6 = 0b001001;
                else if (mnemonic == "vor.vv") funct6 = 0b001010;
                else if (mnemonic == "vxor.vv") funct6 = 0b001011;
                else if (mnemonic == "vror.vv") funct6 = 0b010100;
                else if (mnemonic == "vrol.vv") funct6 = 0b010101;
                else if (mnemonic == "vmseq.vv") funct6 = 0b011000;
                else if (mnemonic == "vmsne.vv") funct6 = 0b011001;
                else if (mnemonic == "vmsltu.vv") funct6 = 0b011010;
                else if (mnemonic == "vmslt.vv") funct6 = 0b011011;
                else throw std::runtime_error("Unsupported vector-vector instruction: " + mnemonic);
                const uint32_t vm = block.getImm() == "0" ? 0u : 1u;
                code = (funct6 << 26) | (vm << 25) | (vs2 << 20) | (vs1 << 15) |
                       (funct3 << 12) | (rd << 7) | 0x57u;
            }
        }
        else if (instruction_set::isValidRTypeInstruction(block.getOpcode()))
        {
            code = generateRTypeMachineCode(block);
        }
        else if (instruction_set::isValidI1TypeInstruction(block.getOpcode()))
        {
            code = generateI1TypeMachineCode(block);
        }
        else if (instruction_set::isValidI2TypeInstruction(block.getOpcode()))
        {
            code = generateI2TypeMachineCode(block);
        }
        else if (instruction_set::isValidI3TypeInstruction(block.getOpcode()))
        {
            code = generateI3TypeMachineCode(block);
        }
        else if (instruction_set::isValidSTypeInstruction(block.getOpcode()))
        {
            code = generateSTypeMachineCode(block);
        }
        else if (instruction_set::isValidBTypeInstruction(block.getOpcode()))
        {
            code = generateBTypeMachineCode(block);
        }
        else if (instruction_set::isValidUTypeInstruction(block.getOpcode()))
        {
            code = generateUTypeMachineCode(block);
        }
        else if (instruction_set::isValidJTypeInstruction(block.getOpcode()))
        {
            code = generateJTypeMachineCode(block);
        }
        else if (instruction_set::isValidCSRRTypeInstruction(block.getOpcode()))
        {
            code = generateCSRRTypeMachineCode(block);
        }
        else if (instruction_set::isValidCSRITypeInstruction(block.getOpcode()))
        {
            code = generateCSRITypeMachineCode(block);
        }
        else if (instruction_set::isValidFDRTypeInstruction(block.getOpcode()))
        {
            code = generateFDRTypeMachineCode(block);
        }
        else if (instruction_set::isValidFDR1TypeInstruction(block.getOpcode()))
        {
            code = generateFDR1TypeMachineCode(block);
        }
        else if (instruction_set::isValidFDR2TypeInstruction(block.getOpcode()))
        {
            code = generateFDR2TypeMachineCode(block);
        }
        else if (instruction_set::isValidFDR3TypeInstruction(block.getOpcode()))
        {
            code = generateFDR3TypeMachineCode(block);
        }
        else if (instruction_set::isValidFDR4TypeInstruction(block.getOpcode()))
        {
            code = generateFDR4TypeMachineCode(block);
        }
        else if (instruction_set::isValidFDITypeInstruction(block.getOpcode()))
        {
            code = generateFDITypeMachineCode(block);
        }
        else if (instruction_set::isValidFDSTypeInstruction(block.getOpcode()))
        {
            code = generateFDSTypeMachineCode(block);
        }
        else
        {
            throw std::runtime_error("Invalid instruction type: " + block.getOpcode());
        }
        machine_code.push_back(code);
    }
    return machine_code;
}
}// namespace Kites