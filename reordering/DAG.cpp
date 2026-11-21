#include "DAG.h"
#include "parser.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <vector>

using namespace std;

namespace
{
// Run the reorder binary from the Kites project root (the folder containing build-cli/ and cli/).
// stdout of the helper commands is silenced, stderr (build errors, profiler warnings) stays visible.
const char *const kBuildCmd = "cmake --build build-cli --target kites_cli -j$(nproc) > /dev/null";
const char *const kProfileCmd =
    "KITES_DAG_NO_PROFILE=1 ./build-cli/kites-cli cli/example.s --profile > /dev/null";
const char *const kPairsFile = "load_store_dep.txt";

bool ran_ok(int rc)
{
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}
} // namespace

DAG::DAG(vector<parsed_instruction> &temp, bool forw)
{
    instr = temp;
    this->forward = forw;
}

// Receives the pairs exported by --profile (load_store_dep.txt): every pair of
// (0-indexed, program-order) instruction indices listed is a load/store or
// store/store pair whose addresses never overlapped, i.e. NO memory dependency.
// Pairs are stored order-independently, so (load, store) / (store, load) /
// (min, max) all work. Call BEFORE depend_matrix_full().
void DAG::set_independent_mem_pairs(const std::set<std::pair<int, int>> &pairs)
{
    no_mem_dep.clear();
    for (const auto &p : pairs)
        no_mem_dep.insert({std::min(p.first, p.second), std::max(p.first, p.second)});
    mem_pairs_ready = true;
}

// Generates load_store_dep.txt with the two shell commands, then reads it back.
// Any failure leaves no_mem_dep empty, i.e. every load/store pair stays dependent (safe).
bool DAG::profile_and_load_mem_pairs()
{
    mem_pairs_ready = true; // try at most once per DAG object
    no_mem_dep.clear();

    // we are the "--profile" child itself: never spawn another profiler
    if (std::getenv("KITES_DAG_NO_PROFILE"))
        return false;

    // never trust a leftover file from an earlier / different program
    std::remove(kPairsFile);

    if (!ran_ok(std::system(kBuildCmd)))
    {
        std::cerr << "[DAG] build failed - keeping all load/store pairs dependent\n";
        return false;
    }
    if (!ran_ok(std::system(kProfileCmd)))
    {
        std::cerr << "[DAG] profiling failed - keeping all load/store pairs dependent\n";
        return false;
    }

    std::ifstream in(kPairsFile);
    if (!in)
    {
        std::cerr << "[DAG] " << kPairsFile << " was not produced - keeping all pairs dependent\n";
        return false;
    }

    const int n = static_cast<int>(instr.size());
    std::set<std::pair<int, int>> pairs;
    int a, b;
    while (in >> a >> b)
    {
        if (a < 0 || b < 0 || a >= n || b >= n)
        {
            std::cerr << "[DAG] pair (" << a << ", " << b << ") is outside this program ("
                      << n << " instructions) - profiled a different .s file? "
                      << "Keeping all pairs dependent\n";
            return false;
        }
        pairs.insert({a, b});
    }
    set_independent_mem_pairs(pairs);
    return true;
}

// true if the profile says these two instructions never touch the same address
bool DAG::no_mem_conflict(const parsed_instruction &a, const parsed_instruction &b) const
{
    return no_mem_dep.count({std::min(a.index, b.index), std::max(a.index, b.index)}) > 0;
}

dpd_format DAG::dep_type(parsed_instruction a, parsed_instruction b)
{

    if (a.format == InstrFormat::Unknown || b.format == InstrFormat::Unknown)
    {
        return dpd_format::CTRL;
    }
    if (a.rd > 0 && (a.rd == b.rs1 || a.rd == b.rs2))
        return dpd_format::RAW;

    if (b.format == InstrFormat::B || b.format == InstrFormat::J)
        return dpd_format::CTRL;

    if (a.rd > 0 && a.rd == b.rd)
        return dpd_format::WAW;
    if (b.rd > 0 && (b.rd == a.rs1 || b.rd == a.rs2))
        return dpd_format::WAR;

    // memory pairs: only a dependency if the profile did NOT clear the pair
    if (a.format == InstrFormat::S && b.format == InstrFormat::S)
        return no_mem_conflict(a, b) ? dpd_format::NA : dpd_format::WAW;
    if (a.format == InstrFormat::L && b.format == InstrFormat::S)
        return no_mem_conflict(a, b) ? dpd_format::NA : dpd_format::WAR;
    if (a.format == InstrFormat::S && b.format == InstrFormat::L)
        return no_mem_conflict(a, b) ? dpd_format::NA : dpd_format::MEM;

    return dpd_format::NA;
}

std::vector<std::vector<parsed_instruction>> DAG::blocks()
{
    int n = instr.size();

    std::vector<bool> leader(n, false);

    if (n > 0)
        leader[0] = true;

    for (int i = 0; i < n; i++)
    {
        if (instr[i].format == InstrFormat::B || instr[i].format == InstrFormat::J)
        {
            if (i + 1 < n)
                leader[i + 1] = true; // after a branch
            int target = i + instr[i].memory_add_offset / 4;
            if (target >= 0 && target < n)
                leader[target] = true; // branch target
        }
    }

    std::vector<std::vector<parsed_instruction>> out;
    std::vector<parsed_instruction> cur;
    for (int i = 0; i < n; i++)
    {
        if (leader[i] && !cur.empty())
        {
            out.push_back(cur);
            cur.clear();
        }
        cur.push_back(instr[i]);
        cur.back().index_in_block = (int)cur.size() - 1; // set on the stored copy
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::vector<std::vector<std::vector<depend_description>>> DAG::depend_matrix_full()
{
    if (!mem_pairs_ready)
        profile_and_load_mem_pairs();

    std::vector<std::vector<std::vector<depend_description>>> ans;
    std::vector<std::vector<parsed_instruction>> block_vector = blocks();
    for (auto bloc : block_vector)
    {
        ans.push_back(depend_matrix_block(bloc));
    }

    return ans;
}

std::vector<std::vector<depend_description>>
DAG::depend_matrix_block(std::vector<parsed_instruction> block)
{
    vector<std::vector<depend_description>> depend_matrix(
        block.size(), std::vector<depend_description>(block.size()));

    for (int i = 0; i < block.size(); i++)
    {
        for (int j = i + 1; j < block.size(); j++)
        {
            depend_matrix[i][j].frmt = dep_type(block[i], block[j]); // i->j RAW
        }
    }

    return depend_matrix;
}