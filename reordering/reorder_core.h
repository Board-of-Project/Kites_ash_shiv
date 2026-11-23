#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace Kites
{
struct ReorderResult
{
    std::vector<uint32_t> words;     // reordered machine code
    std::vector<int> origIndex;      // origIndex[k] = original instruction number in slot k
    int cyclesBefore = 0, cyclesAfter = 0;
    int instrCount = 0;              // executions (weighted) or static count
    int stallsBefore = 0, stallsAfter = 0;
    bool weighted = false;           // true if execution counts were used
};

ReorderResult reorderWords(const std::vector<uint32_t> &text, bool forwarding,
                           const std::vector<long long> &instrCounts = {},
                           const std::vector<std::pair<int, int>> *indepPairs = nullptr);
} // namespace Kites