#include "parser.h"
#include <cstdio>
#include <stdio.h>
#include <string.h>


int main()
{

    FILE* fptr = fopen( "../cli/example.s" , "r");
    std::string hexcode = "";
    char instruct[35];
    while(fscanf(fptr , "%s" , instruct)==1){
       hexcode = hexcode + " " + instruct;
    }
    

    fclose(fptr);

    parser p(hexcode);

    std::vector<parsed_instruction> mewo = p.instructions();

    for (auto it : mewo)
    {
        printf("%i rs1 =  %d rs2 = %d  rd = %d imm = %d mem_off = %d off = %d \n", it.format,
               it.rs1, it.rs2, it.rd, it.imm, it.memory_add_offset, it.offset);
    }

    return 0;
}