#pragma once
#include "parser.h"
#include <unordered_set>
#include <utility>
#include <vector>

enum class dpd_format
{
    NA,
    RAW,
    RAW3,
    WAR,
    WAW,
    MEM,
    CTRL
};

struct depend_description
{
    dpd_format frmt = dpd_format::NA;
};

class DAG
{
    std::vector<parsed_instruction> instr;
    bool forward;
    std::unordered_set<unsigned long long> ld_sd_depen; // sparse: only the independent pairs
    static unsigned long long pairKey(int a, int b)
    {
        if (a > b) std::swap(a, b);
        return (static_cast<unsigned long long>(a) << 32) | static_cast<unsigned int>(b);
    }
    bool indep(int a, int b) const { return ld_sd_depen.count(pairKey(a, b)) != 0; }


  public:
    // indepPairs: optional (index, index) load/store pairs known to be independent.
    // If given, they are used directly and no profile file is read.
    // If null, the pairs are read from $KITES_LD_SD_FILE, or ../load_store_dep.txt.
    explicit DAG(std::vector<parsed_instruction> &temp, bool forw,
                 const std::vector<std::pair<int, int>> *indepPairs = nullptr);

    dpd_format dep_type(parsed_instruction a, parsed_instruction b);

    std::vector<std::vector<parsed_instruction>> blocks();

    std::vector<std::vector<depend_description>>
    depend_matrix_block(std::vector<parsed_instruction> block);

    std::vector<std::vector<std::vector<depend_description>>> depend_matrix_full();
};