#include "heurestic.h"
#include <algorithm>
#include <sstream>

heurestics::heurestics(DelayMatrix delays, DAG_matrix matr)
    : dly(std::move(delays)), dpd(std::move(matr)), n((int)dly.size()),
      succ_count(n, 0), interlocks(n, 0), depth(n, 0)
{
    
    for (int i = n - 1; i >= 0; i--)
        for (int j = i + 1; j < n; j++) {
            if (dpd[i][j].frmt == dpd_format::NA) continue;
            succ_count[i]++;
            if (dly[i][j] > 1) interlocks[i] = 1;          
            depth[i] = std::max(depth[i], dly[i][j] + depth[j]);  
        }
}

void heurestics::set_order(const std::vector<int>& order) {
    order_ = order;
}

std::vector<int> heurestics::parse_order(const std::string& spec) {
    std::vector<int> parsed;
    std::stringstream ss(spec);
    std::string tok;

    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        if (tok == "0" || tok == "stalls")          parsed.push_back(0);
        else if (tok == "1" || tok == "interlocks") parsed.push_back(1);
        else if (tok == "2" || tok == "succ")       parsed.push_back(2);
        else if (tok == "3" || tok == "depth")      parsed.push_back(3);
        else if (tok == "4" || tok == "index")      parsed.push_back(4);
        else {
            throw std::invalid_argument("Unknown heuristic token in HEUR_ORDER: " + tok);
        }
    }
    return parsed.empty() ? std::vector<int>{0, 1, 2, 3, 4} : parsed;
}




int heurestics::get_stalls(int i, const sched_state &st) const
{
    int earliest = st.cycle + 1, t = earliest;
    for (int k = 0; k < i; k++)
        if (dpd[k][i].frmt != dpd_format::NA && st.issue[k] != -1)
            t = std::max(t, st.issue[k] + dly[k][i]);
    return t - earliest;
}


std::vector<int> heurestics::get_heurestic_tuple(int i, const sched_state &st) const
{
    const int vals[5] = {
        get_stalls(i, st),   // 0: avoid interlock with the previous instruction
        -interlocks[i],      // 1: prefer ones that interlock with a successor
        -succ_count[i],      // 2: prefer more successors
        -depth[i],           // 3: prefer longer path to leaf
        i                    // 4: tie-break: original program order
    };

    std::vector<int> tuple;
    tuple.reserve(order_.size());
    for (int idx : order_) {
        if (idx >= 0 && idx < 5) {
            tuple.push_back(vals[idx]);
        }
    }
    return tuple;
}