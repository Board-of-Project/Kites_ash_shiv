// rv64i.cpp
// Generates random (but valid, guaranteed-terminating) RV64I instructions:
//   - R-type and I-type 64-bit & 32-bit (*W) ALU/shift instructions over x0..x23
//   - 64-bit & sub-word loads/stores (ld/lw/lh/lb, sd/sw/sh/sb) using 4 base pointers (x24..x27) in .data (>= 0x10000000)
//   - Optional strictly-forward conditional branches (beq/bne/blt/bge/bltu/bgeu) & jal (--branch / --ctrl <pct>)
//   - Optional jump-isolated bounded countdown loops (--loop) using x28 and signed `blt x0, x28`
//
// Usage:
//   ./rv64i_gen [count] [seed] [mem%] [ctrl%] [--branch] [--loop] [--no-subword] [--no-w]
//
// Outputs:
//   rv64i_instructions.txt  -> 32-bit hex words, one per line
//   rv64i_assembly.txt      -> matching assembly text, one per line
//   rv64i_mem_base.txt      -> 4 random 8-byte aligned .data base addresses (x24..x27)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

using u32 = uint32_t;
using u64 = uint64_t;

// ---------- opcodes ----------
enum : u32 {
    OP_LOAD   = 0x03,
    OP_IMM    = 0x13,
    OP_IMM32  = 0x1B, // RV64I 32-bit immediate ALU/shift opcode (addiw, slliw, srliw, sraiw)
    OP_STORE  = 0x23,
    OP_REG    = 0x33,
    OP_LUI    = 0x37,
    OP_REG32  = 0x3B, // RV64I 32-bit register ALU/shift opcode (addw, subw, sllw, srlw, sraw)
    OP_BRANCH = 0x63,
    OP_JAL    = 0x6F
};

// ---------- encoders ----------
static u32 encR(u32 f7, u32 rs2, u32 rs1, u32 f3, u32 rd, u32 op) {
    return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
static u32 encI(u32 imm, u32 rs1, u32 f3, u32 rd, u32 op) {
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op;
}
static u32 encS(u32 imm, u32 rs2, u32 rs1, u32 f3, u32 op) {
    return (((imm >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) |
           ((imm & 0x1F) << 7) | op;
}
static u32 encB(u32 imm, u32 rs2, u32 rs1, u32 f3, u32 op) {
    imm &= 0x1FFF;
    u32 b12   = (imm >> 12) & 0x1;
    u32 b11   = (imm >> 11) & 0x1;
    u32 b10_5 = (imm >> 5) & 0x3F;
    u32 b4_1  = (imm >> 1) & 0xF;
    return (b12 << 31) | (b10_5 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) |
           (b4_1 << 8) | (b11 << 7) | op;
}
static u32 encU(u32 imm20, u32 rd, u32 op) {
    return ((imm20 & 0xFFFFF) << 12) | (rd << 7) | op;
}
static u32 encJ(u32 imm, u32 rd, u32 op) {
    imm &= 0x1FFFFF;
    u32 b20    = (imm >> 20) & 0x1;
    u32 b19_12 = (imm >> 12) & 0xFF;
    u32 b11    = (imm >> 11) & 0x1;
    u32 b10_1  = (imm >> 1) & 0x3FF;
    return (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12) | (rd << 7) | op;
}

// ---------- helpers ----------
static int32_t sext(u32 v, int bits) {
    u32 m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return (int32_t)((v ^ m) - m);
}
static std::string X(u32 r) { return "x" + std::to_string(r); }

struct Instr {
    u32 word;
    std::string text;
};

static void emit(const Instr& ins, std::ofstream& out, std::ofstream& asmOut) {
    out << std::hex << std::setw(8) << std::setfill('0') << ins.word << "\n";
    asmOut << ins.text << "\n";
}

// ---------- random instruction generator ----------
static std::mt19937 rng;

static u32 rnd(u32 lo, u32 hi) {  // inclusive
    return std::uniform_int_distribution<u32>(lo, hi)(rng);
}

// Boundary-biased 12-bit immediate generator (~20% corner cases: 0, 1, -1, 2047, -2048, 8, -8)
static u32 rndImm12() {
    if (rnd(0, 99) < 20) {
        static const u32 edgeImm[] = {0x000, 0x001, 0xFFF, 0x7FF, 0x800, 0x008, 0xFF8};
        return edgeImm[rnd(0, 6)];
    }
    return rnd(0, 0xFFF);
}

// Boundary-biased shift amount generator (~20% corner cases)
static u32 rndShamt(bool is32Bit) {
    if (rnd(0, 99) < 20) {
        if (is32Bit) {
            static const u32 edge32[] = {0, 1, 15, 31};
            return edge32[rnd(0, 3)];
        } else {
            static const u32 edge64[] = {0, 1, 31, 32, 63};
            return edge64[rnd(0, 4)];
        }
    }
    return is32Bit ? rnd(0, 31) : rnd(0, 63);
}

// General-purpose registers x0..x23 are used for ALU, branch/jal, and ld/sd data operands.
// Registers x24..x27 are dedicated memory base pointers into .data (>= 0x10000000).
// Register x28 is reserved as a dedicated bounded-loop counter (never touched by ALU/mem ops).
static const u32 MAX_REG    = 23;
static const u32 BASE_START = 24;
static const u32 NUM_BASES  = 4;
static const u32 LOOP_REG   = 28;
static const u32 MEM_SLOTS  = 8;  // 8 double-words (64 bytes): offsets 0, 8, ..., 56

static int memPercent     = 25;
static int ctrlPercent    = 0;
static bool enableLoops   = false; // [UPDATED] Opt-in via --loop; default --branch uses strictly forward DAG branches/jal
static bool enableSubword = true;
static bool enableW       = true;

static u32 reg() { return rnd(0, MAX_REG); }
static u32 baseReg() { return BASE_START + rnd(0, NUM_BASES - 1); }

static Instr memoryInstruction() {
    u32 base = baseReg();
    bool isLoad = (rnd(0, 1) == 0);

    u32 widthRoll = enableSubword ? rnd(0, 99) : 0;
    if (widthRoll < 70) {
        u32 off = rnd(0, MEM_SLOTS - 1) * 8;
        if (isLoad) {
            u32 rd = reg();
            return {encI(off, base, 3, rd, OP_LOAD),
                    "ld " + X(rd) + ", " + std::to_string(off) + "(" + X(base) + ")"};
        }
        u32 rs2 = reg();
        return {encS(off, rs2, base, 3, OP_STORE),
                "sd " + X(rs2) + ", " + std::to_string(off) + "(" + X(base) + ")"};
    } else if (widthRoll < 82) {
        u32 off = rnd(0, 15) * 4;
        if (isLoad) {
            u32 rd = reg();
            return {encI(off, base, 2, rd, OP_LOAD),
                    "lw " + X(rd) + ", " + std::to_string(off) + "(" + X(base) + ")"};
        }
        u32 rs2 = reg();
        return {encS(off, rs2, base, 2, OP_STORE),
                "sw " + X(rs2) + ", " + std::to_string(off) + "(" + X(base) + ")"};
    } else if (widthRoll < 91) {
        u32 off = rnd(0, 31) * 2;
        if (isLoad) {
            u32 rd = reg();
            return {encI(off, base, 1, rd, OP_LOAD),
                    "lh " + X(rd) + ", " + std::to_string(off) + "(" + X(base) + ")"};
        }
        u32 rs2 = reg();
        return {encS(off, rs2, base, 1, OP_STORE),
                "sh " + X(rs2) + ", " + std::to_string(off) + "(" + X(base) + ")"};
    } else {
        u32 off = rnd(0, 63);
        if (isLoad) {
            u32 rd = reg();
            return {encI(off, base, 0, rd, OP_LOAD),
                    "lb " + X(rd) + ", " + std::to_string(off) + "(" + X(base) + ")"};
        }
        u32 rs2 = reg();
        return {encS(off, rs2, base, 0, OP_STORE),
                "sb " + X(rs2) + ", " + std::to_string(off) + "(" + X(base) + ")"};
    }
}

// [UPDATED] Also outputs `outInstrSkip` so the main loop can prevent backward loops from overlapping forward jump targets
static Instr forwardControlInstruction(u32 remainingAfter, u32& outInstrSkip) {
    // Strictly forward jump skipping 1..min(remainingAfter, 6) instructions (+8..+28 bytes).
    // Because target PC > current PC, forward-only control flow forms an acyclic DAG and never loops.
    u32 maxSkip = std::min<u32>(remainingAfter, 6);
    u32 skip = rnd(1, maxSkip);
    outInstrSkip = skip + 1;
    u32 byteOff = outInstrSkip * 4;

    if (rnd(0, 2) < 2) {  // 2/3 Conditional Branch (B-type)
        static const char* nm[] = {"beq", "bne", "blt", "bge", "bltu", "bgeu"};
        static const u32 f3[]   = {0, 1, 4, 5, 6, 7};
        u32 i = rnd(0, 5);
        u32 rs1 = reg(), rs2 = reg();
        return {encB(byteOff, rs2, rs1, f3[i], OP_BRANCH),
                std::string(nm[i]) + " " + X(rs1) + ", " + X(rs2) + ", " + std::to_string(byteOff)};
    } else {  // 1/3 Unconditional Jump and Link (J-type)
        u32 rd = reg();
        return {encJ(byteOff, rd, OP_JAL),
                "jal " + X(rd) + ", " + std::to_string(byteOff)};
    }
}

static Instr aluInstruction() {
    if (enableW && rnd(0, 99) < 25) {
        if (rnd(0, 1) == 0) {
            static const char* nm[] = {"addw", "subw", "sllw", "srlw", "sraw"};
            static const u32 f3[]   = {0, 0, 1, 5, 5};
            static const u32 f7[]   = {0x00, 0x20, 0x00, 0x00, 0x20};
            u32 i = rnd(0, 4);
            u32 rd = reg(), rs1 = reg(), rs2 = reg();
            return {encR(f7[i], rs2, rs1, f3[i], rd, OP_REG32),
                    std::string(nm[i]) + " " + X(rd) + ", " + X(rs1) + ", " + X(rs2)};
        } else {
            u32 k = rnd(0, 3);
            u32 rs1 = reg(), rd = reg();
            if (k == 0) {
                u32 imm = rndImm12();
                return {encI(imm, rs1, 0, rd, OP_IMM32),
                        "addiw " + X(rd) + ", " + X(rs1) + ", " + std::to_string(sext(imm, 12))};
            }
            static const char* nm[] = {"slliw", "srliw", "sraiw"};
            u32 f3 = (k == 1) ? 1 : 5;
            u32 hi = (k == 3) ? 0x20 : 0x00;
            u32 shamt = rndShamt(true);
            u32 imm = (hi << 5) | shamt;
            return {encI(imm, rs1, f3, rd, OP_IMM32),
                    std::string(nm[k - 1]) + " " + X(rd) + ", " + X(rs1) + ", " + std::to_string(shamt)};
        }
    }

    switch (rnd(0, 2)) {
    case 0: {
        static const char* nm[] = {"add", "sub", "sll", "slt", "sltu",
                                   "xor", "srl", "sra", "or",  "and"};
        static const u32 f3[] = {0, 0, 1, 2, 3, 4, 5, 5, 6, 7};
        static const u32 f7[] = {0x00, 0x20, 0, 0, 0, 0, 0x00, 0x20, 0, 0};
        u32 i = rnd(0, 9);
        u32 rd = reg(), rs1 = reg(), rs2 = reg();
        return {encR(f7[i], rs2, rs1, f3[i], rd, OP_REG),
                std::string(nm[i]) + " " + X(rd) + ", " + X(rs1) + ", " + X(rs2)};
    }
    case 1: {
        static const char* nm[] = {"addi", "slti", "sltiu", "xori", "ori", "andi"};
        static const u32 f3[] = {0, 2, 3, 4, 6, 7};
        u32 k = rnd(0, 5), imm = rndImm12();
        u32 rs1 = reg(), rd = reg();
        return {encI(imm, rs1, f3[k], rd, OP_IMM),
                std::string(nm[k]) + " " + X(rd) + ", " + X(rs1) + ", " +
                    std::to_string(sext(imm, 12))};
    }
    default: {
        static const char* nm[] = {"slli", "srli", "srai"};
        u32 k = rnd(0, 2);
        u32 f3 = (k == 0) ? 1 : 5;
        u32 hi = (k == 2) ? 0x10 : 0x00;
        u32 shamt = rndShamt(false);
        u32 imm = (hi << 6) | shamt;
        u32 rs1 = reg(), rd = reg();
        return {encI(imm, rs1, f3, rd, OP_IMM),
                std::string(nm[k]) + " " + X(rd) + ", " + X(rs1) + ", " + std::to_string(shamt)};
    }
    }
}

static Instr nonControlInstruction() {
    int nonCtrlTotal = 100 - ctrlPercent;
    if (nonCtrlTotal > 0 && (int)rnd(0, nonCtrlTotal - 1) < memPercent) {
        return memoryInstruction();
    }
    return aluInstruction();
}

int main(int argc, char** argv) {
    int count = 1000;
    u32 seed = std::random_device{}();
    int posIdx = 0;

    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--branch") == 0 || std::strcmp(argv[a], "--ctrl") == 0) {
            if (ctrlPercent == 0) ctrlPercent = 15;
            if (a + 1 < argc && argv[a + 1][0] != '-' && std::strcmp(argv[a], "--ctrl") == 0) {
                ctrlPercent = std::atoi(argv[++a]);
            }
        } else if (std::strncmp(argv[a], "--ctrl=", 7) == 0) {
            ctrlPercent = std::atoi(argv[a] + 7);
        } else if (std::strcmp(argv[a], "--loop") == 0) { // [ADDED] Opt-in flag for bounded backward loops
            enableLoops = true;
            if (ctrlPercent == 0) ctrlPercent = 15;
        } else if (std::strcmp(argv[a], "--no-subword") == 0) {
            enableSubword = false;
        } else if (std::strcmp(argv[a], "--no-w") == 0) {
            enableW = false;
        } else {
            if (posIdx == 0) count = std::atoi(argv[a]);
            else if (posIdx == 1) seed = (u32)std::strtoul(argv[a], nullptr, 10);
            else if (posIdx == 2) memPercent = std::atoi(argv[a]);
            else if (posIdx == 3) ctrlPercent = std::atoi(argv[a]);
            ++posIdx;
        }
    }

    rng.seed(seed);

    std::ofstream out("rv64i_instructions.txt");
    if (!out) {
        std::cerr << "Could not open rv64i_instructions.txt\n";
        return 1;
    }
    std::ofstream asmOut("rv64i_assembly.txt");
    if (!asmOut) {
        std::cerr << "Could not open rv64i_assembly.txt\n";
        return 1;
    }

    // 1. Randomly initialize general-purpose registers x1..x23 with non-zero 64-bit values
    for (u32 r = 1; r <= MAX_REG; ++r) {
        u32 hi20 = rnd(1, 0xFFFFF);
        std::ostringstream hexImm;
        hexImm << "0x" << std::hex << hi20;
        emit({encU(hi20, r, OP_LUI), "lui " + X(r) + ", " + hexImm.str()}, out, asmOut);

        u32 lo12 = rnd(1, 0xFFF);
        emit({encI(lo12, r, 0, r, OP_IMM),
              "addi " + X(r) + ", " + X(r) + ", " + std::to_string(sext(lo12, 12))},
             out, asmOut);
    }

    // 2. Initialize x24..x27 to 4 random, distinct 8-byte aligned addresses in Kites' .data segment
    std::ofstream baseOut("rv64i_mem_base.txt");
    for (u32 b = 0; b < NUM_BASES; ++b) {
        u32 baseRegId = BASE_START + b;
        u32 baseHi20 = 0x10000 + b * 0x10 + rnd(0, 0x0F);
        u32 baseLo12 = rnd(0, 255) * 8;
        u64 memBaseAddr = (static_cast<u64>(baseHi20) << 12) + baseLo12;

        std::ostringstream hexPage;
        hexPage << "0x" << std::hex << baseHi20;
        emit({encU(baseHi20, baseRegId, OP_LUI), "lui " + X(baseRegId) + ", " + hexPage.str()},
             out, asmOut);
        emit({encI(baseLo12, baseRegId, 0, baseRegId, OP_IMM),
              "addi " + X(baseRegId) + ", " + X(baseRegId) + ", " + std::to_string(baseLo12)},
             out, asmOut);

        if (baseOut) {
            baseOut << "0x" << std::hex << memBaseAddr << "\n";
        }
    }

    // 3. Prologue: randomly seed all 8 double-word memory slots across all 4 base pointers
    if (memPercent > 0) {
        for (u32 b = 0; b < NUM_BASES; ++b) {
            u32 baseRegId = BASE_START + b;
            for (u32 k = 0; k < MEM_SLOTS; ++k) {
                u32 off = k * 8;
                u32 srcReg = rnd(1, MAX_REG);
                emit({encS(off, srcReg, baseRegId, 3, OP_STORE),
                      "sd " + X(srcReg) + ", " + std::to_string(off) + "(" + X(baseRegId) + ")"},
                     out, asmOut);
            }
        }
    }

    // 4. [UPDATED] Main random instruction body:
    //    - Default (--branch / --ctrl): emits strictly forward branches & jal (acyclic DAG, 0 loops possible).
    //    - With --loop: emits bounded countdown loops ONLY at/past `lastForwardTarget` (so no earlier
    //      forward branch/jal can jump into a loop body) and uses signed `blt x0, x28` so x28 <= 0 always exits.
    int emitted = 0;
    int lastForwardTarget = 0; // [ADDED] Furthest instruction index targeted by any prior forward jump
    while (emitted < count) {
        u32 rem = static_cast<u32>((count - 1) - emitted);
        int roll = (int)rnd(0, 99);

        if (ctrlPercent > 0 && rem >= 1 && roll < ctrlPercent) {
            if (enableLoops && emitted >= lastForwardTarget && rem >= 5 && rnd(0, 3) == 0) {
                u32 iters = rnd(2, 3);
                u32 bodyLen = std::min<u32>(rnd(2, 4), rem - 3);
                emit({encI(iters, 0, 0, LOOP_REG, OP_IMM),
                      "addi " + X(LOOP_REG) + ", x0, " + std::to_string(iters)},
                     out, asmOut);
                ++emitted;

                for (u32 j = 0; j < bodyLen; ++j) {
                    emit(nonControlInstruction(), out, asmOut);
                    ++emitted;
                }

                // [UPDATED] Decrement x28 and branch backward only while 0 < x28 (signed blt prevents underflow)
                u32 negImm = static_cast<u32>(-static_cast<int32_t>((bodyLen + 1) * 4));
                int32_t negOff = -static_cast<int32_t>((bodyLen + 1) * 4);
                emit({encI(0xFFF, LOOP_REG, 0, LOOP_REG, OP_IMM),
                      "addi " + X(LOOP_REG) + ", " + X(LOOP_REG) + ", -1"},
                     out, asmOut);
                ++emitted;

                emit({encB(negImm, LOOP_REG, 0, 4, OP_BRANCH),
                      "blt x0, " + X(LOOP_REG) + ", " + std::to_string(negOff)},
                     out, asmOut);
                ++emitted;
            } else {
                u32 skipInstrs = 0;
                emit(forwardControlInstruction(rem, skipInstrs), out, asmOut);
                lastForwardTarget = std::max(lastForwardTarget, emitted + static_cast<int>(skipInstrs));
                ++emitted;
            }
        } else {
            emit(nonControlInstruction(), out, asmOut);
            ++emitted;
        }
    }

    std::cout << "Wrote " << emitted << " random instructions (mem=" << memPercent
              << "%, ctrl=" << ctrlPercent << "%, seed=" << seed << ")\n";
    return 0;
}