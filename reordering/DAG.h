#pragma once
#include "parser.h"
#include <set>
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

    // (min, max) index pairs of load/store and store/store with NO memory dependency
    std::set<std::pair<int, int>> no_mem_dep;
    bool no_mem_conflict(const parsed_instruction &a, const parsed_instruction &b) const;

    // true once the pairs are in place (supplied by hand, or profiled) - profiling runs at most once
    bool mem_pairs_ready = false;
    // system(): build kites_cli, run "--profile" to produce load_store_dep.txt, read it back
    bool profile_and_load_mem_pairs();

  public:
    explicit DAG(std::vector<parsed_instruction> &temp, bool forw);

    // optional manual override of the profiled pairs; call BEFORE depend_matrix_full()
    void set_independent_mem_pairs(const std::set<std::pair<int, int>> &pairs);

    dpd_format dep_type(parsed_instruction a, parsed_instruction b);

    std::vector<std::vector<parsed_instruction>> blocks();

    std::vector<std::vector<depend_description>>
    depend_matrix_block(std::vector<parsed_instruction> block);

    std::vector<std::vector<std::vector<depend_description>>> depend_matrix_full();
};