// rv64i_gen.cpp
// Generates random (but valid) RV64I instructions, limited to 
// R-type and I-type instructions only.
//
// Outputs (both written at the same time, line N of each file is the same instruction):
//   rv64i_instructions.txt  -> 32-bit hex words, one per line (this is what the tester reads)
//   rv64i_assembly.txt      -> the matching assembly text, one per line (for reference only)
//
// Build: g++ -std=c++17 -o rv64i_gen rv64i.cpp
// Run:   ./rv64i_gen [count] [seed]     (defaults: 1000, random seed)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

using u32 = uint32_t;

// ---------- opcodes ----------
enum : u32 {
    OP_IMM    = 0x13,
    OP_REG    = 0x33
};

// ---------- encoders ----------
static u32 encR(u32 f7, u32 rs2, u32 rs1, u32 f3, u32 rd, u32 op) {
    return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
static u32 encI(u32 imm, u32 rs1, u32 f3, u32 rd, u32 op) {
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}

// ---------- helpers for the assembly text ----------
// interpret the low `bits` bits of v as a signed number
static int32_t sext(u32 v, int bits) {
    u32 m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return (int32_t)((v ^ m) - m);
}
static std::string X(u32 r) { return "x" + std::to_string(r); }

// one generated instruction: machine word + its assembly text
struct Instr {
    u32 word;
    std::string text;
};

// ---------- random instruction generator ----------
static std::mt19937 rng;

static u32 rnd(u32 lo, u32 hi) {  // inclusive
    return std::uniform_int_distribution<u32>(lo, hi)(rng);
}
static const u32 MAX_REG = 5;  // registers x0..x5 only
static u32 reg() { return rnd(0, MAX_REG); }

static Instr randomInstruction() {
    switch (rnd(0, 2)) {
    case 0: {  // R-type: ADD SUB SLL SLT SLTU XOR SRL SRA OR AND
        static const char* nm[] = {"add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and"};
        static const u32 f3[] = {0, 0, 1, 2, 3, 4, 5, 5, 6, 7};
        static const u32 f7[] = {0x00, 0x20, 0, 0, 0, 0, 0x00, 0x20, 0, 0};
        u32 i = rnd(0, 9);
        u32 rd = reg(), rs1 = reg(), rs2 = reg();
        return {encR(f7[i], rs2, rs1, f3[i], rd, OP_REG),
                std::string(nm[i]) + " " + X(rd) + ", " + X(rs1) + ", " + X(rs2)};
    }
    case 1: {  // I-type ALU: ADDI SLTI SLTIU XORI ORI ANDI
        static const char* nm[] = {"addi", "slti", "sltiu", "xori", "ori", "andi"};
        static const u32 f3[] = {0, 2, 3, 4, 6, 7};
        u32 k = rnd(0, 5), imm = rnd(0, 0xFFF);
        u32 rs1 = reg(), rd = reg();
        return {encI(imm, rs1, f3[k], rd, OP_IMM),
                std::string(nm[k]) + " " + X(rd) + ", " + X(rs1) + ", " + std::to_string(sext(imm, 12))};
    }
    default: {  // Shift immediates (6-bit shamt on RV64): SLLI SRLI SRAI
        static const char* nm[] = {"slli", "srli", "srai"};
        u32 k = rnd(0, 2);
        u32 f3 = (k == 0) ? 1 : 5;
        u32 hi = (k == 2) ? 0x10 : 0x00;  // bit 30 set for SRAI
        u32 shamt = rnd(0, 63);
        u32 imm = (hi << 6) | shamt;
        u32 rs1 = reg(), rd = reg();
        return {encI(imm, rs1, f3, rd, OP_IMM),
                std::string(nm[k]) + " " + X(rd) + ", " + X(rs1) + ", " + std::to_string(shamt)};
    }
    }
}

int main(int argc, char** argv) {
    int count = (argc > 1) ? std::atoi(argv[1]) : 1000;
    u32 seed  = (argc > 2) ? (u32)std::strtoul(argv[2], nullptr, 10)
                           : std::random_device{}();
    rng.seed(seed);

    std::ofstream out("rv64i_instructions.txt");
    if (!out) {
        std::cerr << "Could not open rv64i_instructions.txt\n";
        return 1;
    }
    std::ofstream asmOut("rv64i_assembly.txt");  // reference only, never read by the program
    if (!asmOut) {
        std::cerr << "Could not open rv64i_assembly.txt\n";
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        Instr ins = randomInstruction();
        out << std::hex << std::setw(8) << std::setfill('0') << ins.word << "\n";
        asmOut << ins.text << "\n";
    }

    std::cout << "Wrote " << count << " R-type/I-type instructions to "
              << "rv64i_instructions.txt (seed = " << std::dec << seed << ")\n"
              << "Wrote matching assembly to rv64i_assembly.txt\n";
    return 0;
}
