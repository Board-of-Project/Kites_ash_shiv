#include "reorder_core.h"
#include "DAG.h"
#include "parser.h"
#include "scheduler.h"
#include <cstdio>
#include <string>

namespace Kites
{
static std::string toHexString(const std::vector<uint32_t> &words)
{
    std::string s;
    char buf[16];
    for (uint32_t w : words)
    {
        snprintf(buf, sizeof buf, "0x%08x ", w);
        s += buf;
    }
    return s;
}

ReorderResult reorderWords(const std::vector<uint32_t> &text, bool forwarding,
                           const std::vector<long long> &instrCounts)
{
    parser p(toHexString(text));
    std::vector<parsed_instruction> instrs = p.instructions();
    DAG dag(instrs, forwarding);
    std::vector<std::vector<parsed_instruction>> blocks = dag.blocks();
    scheduler s(blocks, dag.depend_matrix_full(), forwarding);

    // weight of a block = executions of its first instruction (all instructions in a block run equally often)
    std::vector<long long> w;
    long long dynInstrs = 0;
    size_t start = 0;
    for (const auto &blk : blocks)
    {
        const long long c =
            instrCounts.empty() ? 1 : (start < instrCounts.size() ? instrCounts[start] : 0);
        w.push_back(c);
        dynInstrs += c * static_cast<long long>(blk.size());
        start += blk.size();
    }
    s.set_weights(w);

    ReorderResult r;
    std::vector<parsed_instruction> out = s.code_scheduler(&r.cyclesBefore, &r.cyclesAfter);
    for (const auto &pi : out)
    {
        r.words.push_back(static_cast<uint32_t>(std::stoul(pi.inst, nullptr, 2)));
        r.origIndex.push_back(pi.index);
    }
    r.instrCount = static_cast<int>(dynInstrs);
    r.stallsBefore = r.cyclesBefore - r.instrCount;
    r.stallsAfter = r.cyclesAfter - r.instrCount;
    r.weighted = !instrCounts.empty();
    return r;
}
} // namespace Kites