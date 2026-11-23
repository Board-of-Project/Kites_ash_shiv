#pragma once
#include "parser.h"
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
    std::vector<std::vector<int>>ld_sd_depen;


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