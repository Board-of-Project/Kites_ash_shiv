#include "DAG.h"
#include "parser.h"
#include "scheduler.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Reads the file produced by rv64i_gen (one 32-bit hex word per line, e.g.
// "10000093") and converts it into the single string format that the parser
// expects: "0x10000093 0x20000113 ... " (each word prefixed with 0x and
// followed by a space).
static bool load_hexcode(const std::string &path, std::string &hexcode)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "Could not open input file: " << path << "\n";
        return false;
    }

    std::string line;
    size_t line_no = 0;

    while (std::getline(in, line))
    {
        ++line_no;

        // allow comments / blank lines in the file
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        if (line[first] == '#' || line.compare(first, 2, "//") == 0)
            continue;

        std::istringstream iss(line);
        std::string token;
        while (iss >> token)
        {
            // strip an existing 0x / 0X prefix so we never double it
            if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
                token = token.substr(2);

            // sanity check: must be hex digits only
            for (char c : token)
            {
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                {
                    std::cerr << "Invalid hex word '" << token << "' on line " << line_no << "\n";
                    return false;
                }
            }

            hexcode += "0x" + token + " ";
        }
    }

    if (hexcode.empty())
    {
        std::cerr << "No instructions found in " << path << "\n";
        return false;
    }

    return true;
}

int main(int argc, char **argv)
{
    // usage: ./tester [path_to_instruction_file]
    // default is the file written by rv64i_gen
    std::string path = (argc > 1) ? argv[1] : "rv64i_instructions.txt";

    std::string hexcode;
    if (!load_hexcode(path, hexcode))
        return 1;

    parser p(hexcode);
    std::vector<parsed_instruction> instr = p.instructions();
    int instr_count = static_cast<int>(instr.size());

    int before = 0;
    int after = 0;

    // ---- with forwarding ----
    bool forwarding = 1;
    DAG dag_fwd(instr, forwarding);
    scheduler s_fwd(dag_fwd.blocks(), dag_fwd.depend_matrix_full(), forwarding);
    std::vector<parsed_instruction> reordered_code = s_fwd.code_scheduler(&before, &after);

    printf("stalls before = %d\n", before - instr_count);
    printf("with forwarding stalls after reordering  = %d\n", after - instr_count);

    // ---- without forwarding ----
    forwarding = 0;
    before = 0;
    after = 0;
    DAG dag_nofwd(instr, forwarding);
    scheduler s_nofwd(dag_nofwd.blocks(), dag_nofwd.depend_matrix_full(), forwarding);
    reordered_code = s_nofwd.code_scheduler(&before, &after);
     
    FILE *fptr = fopen("data.txt", "a");
    fprintf(fptr , "%d %d\n" , before - instr_count , after - instr_count);
    fclose(fptr);

   // printf("stalls before = %d\n", before - instr_count);
   // printf("without forwarding stalls after reordering  = %d\n", after - instr_count);

    return 0;
}

