
#include "DAG.h"
#include "parser.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;

DAG::DAG(vector<parsed_instruction> &temp, bool forw,
         const std::vector<std::pair<int, int>> *indepPairs)
{
    instr = temp;
    this->forward = forw;
    int n = instr.size();

    // size the matrix first (it was never allocated, which caused the segfault)
    ld_sd_depen.assign(n, std::vector<int>(n, 0));

    // pairs supplied directly by the caller (e.g. kites-cli --reorder --with-profile)
    if (indepPairs)
    {
        for (const auto &pr : *indepPairs)
        {
            const int px = pr.first, py = pr.second;
            if (px < 0 || py < 0 || px >= n || py >= n)
                continue;
            ld_sd_depen[px][py] = 1;
            ld_sd_depen[py][px] = 1;
        }
        return;
    }

    // otherwise read pairs profiled as independent from a file:
    // $KITES_LD_SD_FILE if set, else ../load_store_dep.txt
    const char *envPath = std::getenv("KITES_LD_SD_FILE");
    const std::string depPath = (envPath && *envPath) ? envPath : "../load_store_dep.txt";
    FILE* fptr = fopen(depPath.c_str() , "r");
    if (!fptr)
    {
        fprintf(stderr, "[DAG] cannot open %s - keeping all load/store pairs dependent\n", depPath.c_str());
        return;
    }
    int x , y;
    while(fscanf(fptr , "%d %d" , &x , &y)==2){
        if (x < 0 || y < 0 || x >= n || y >= n)
            continue; // index outside this program (stale file?)
        ld_sd_depen[x][y] = 1;
        ld_sd_depen[y][x] = 1;
    }

    fclose(fptr);

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

    if (a.format == InstrFormat::S && b.format == InstrFormat::S){
        if(ld_sd_depen[a.index][b.index]==0)
        return dpd_format::MEM;
        
    }
    if (a.format == InstrFormat::L && b.format == InstrFormat::S){
        if(ld_sd_depen[a.index][b.index]==0)
        return dpd_format::MEM;
    }
    if (a.format == InstrFormat::S && b.format == InstrFormat::L){
        if(ld_sd_depen[a.index][b.index]==0)
        return dpd_format::MEM;
    }

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
        if (instr[i].format == InstrFormat::B || instr[i].format == InstrFormat::J || instr[i].opcode==0b1100111) 
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
    std::vector<std::vector<std::vector<depend_description>>> ans;
    std::vector<std::vector<parsed_instruction>> block_vector = blocks();

    
    
    //int n = instr.size();
    //std::vector<std::vector<int>>ld_sd_depen(n , std::vector<int>(n));
    



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
