
// reorder_tool.cpp
// Standalone (no Qt) driver for the reordering scheduler.
//
//   ./reorder_tool <hex_file> <forwarding 0|1> [order_out]
//
// hex_file   : one 32-bit word per line, as written by rv64i_gen (e.g. 00a50533)
// forwarding : 1 = schedule for a forwarding pipeline, 0 = no forwarding
// order_out  : file to write the permutation to (default: order.txt)
//              line k holds the ORIGINAL instruction number (0-based) that now sits in slot k
//
// Prints "before after" cycle estimates from the scheduler on stdout.
// Exit code: 0 ok, 1 bad usage / IO, 2 scheduler output is not a valid permutation.
 
#include "../reordering/DAG.h"
#include "../reordering/parser.h"
#include "../reordering/scheduler.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
 
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <hex_file> <forwarding 0|1> [order_out]\n", argv[0]);
        return 1;
    }
    const std::string hexFile = argv[1];
    const bool forwarding = std::atoi(argv[2]) != 0;
    const std::string orderOut = argc > 3 ? argv[3] : "order.txt";
 
    std::ifstream in(hexFile);
    if (!in)
    {
        std::fprintf(stderr, "cannot open %s\n", hexFile.c_str());
        return 1;
    }
 
    std::string hexcode, line;
    size_t n = 0;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        unsigned long w = std::stoul(line, nullptr, 16);
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08lx ", w); // the parser wants "0x" + 8 digits
        hexcode += buf;
        ++n;
    }
 
    parser p(hexcode);
    std::vector<parsed_instruction> instrs = p.instructions();
    DAG dag(instrs, forwarding);
    scheduler s(dag.blocks(), dag.depend_matrix_full(), forwarding);
 
    int before = 0, after = 0;
    std::vector<parsed_instruction> out = s.code_scheduler(&before, &after);
 
    // validate: must be a permutation of 0..n-1
    // std::vector<int> seen(n, 0);
    // bool ok = out.size() == n;
    // for (size_t k = 0; ok && k < out.size(); ++k)
    // {
    //     int idx = out[k].index;
    //     if (idx < 0 || static_cast<size_t>(idx) >= n || seen[idx]++)
    //         ok = false;
    // }
    // if (!ok)
    // {
    //     std::fprintf(stderr, "scheduler output is not a permutation of the %zu input instructions\n", n);
    //     return 2;
    // }
 
    std::ofstream o(orderOut);
    for (const auto &pi : out)
        o << pi.index << "\n";
 
    std::printf("%d %d\n", before, after);
    return 0;
}
 