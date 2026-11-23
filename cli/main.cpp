/**
 * @file main.cpp
 * @brief Entry point for the Kites command-line interface.
 */

#include "../reordering/DAG.h"
#include "../reordering/parser.h"
#include "../reordering/scheduler.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>

#include "assembler/assembler.h"
#include "command_handler/command_handler.h"
#include "common/assembled_program.h"
#include "common/globals.h"
#include "common/reorder_program.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "repl.h"
#include "utils/utils.h"

#include <QApplication>
#include <QObject>
#include <QString>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
void printUsage(const char *programName)
{
    std::cerr
        << "Usage: " << programName
        << " [assembly-file] [--vm <n>] [--mem-dump <hex_addr> <row_count>]... [--repl|-i]"
        << " [--reorder] [--profile|--reorder-deps]\n"
        << "  assembly-file   optional; if given without --repl, assembles, runs to\n"
        << "                  completion, and dumps state (batch mode)\n"
        << "  --vm <n>        rvss (default) | rv5-nh-nf | rv5-h-nf | rv5-nh-f | rv5-h-f\n"
        << "  --mem-dump      dump <row_count> 8-byte rows starting at <hex_addr>; "
           "repeatable (batch mode only)\n"
        << "  --repl, -i      drop into the interactive REPL instead of auto-running;\n"
        << "                  if assembly-file is given it is loaded before the prompt\n"
        << "  (running with no assembly-file also enters the REPL)\n"
        << "  --reorder       reorder instructions within basic blocks to cut stalls before\n"
        << "                  running (only with rv5-h-nf / rv5-h-f)\n"
        << "  --profile       run program step-by-step to detect non-overlapping load/store\n"
        << "                  pairs and export them to load_store_dep.txt\n"
        << "  --reorder-deps  read load_store_dep.txt, build the DAG, and print reordered code\n";
}

// Formats program.text_buffer into the whitespace-separated "0xXXXXXXXX" string expected by parser
std::string buildHexCodeText(const Kites::AssembledProgram &program)
{
    std::ostringstream oss;
    auto appendWord = [&oss](const auto &word) {
        using WordT = std::decay_t<decltype(word)>;
        if constexpr (std::is_integral_v<WordT>)
        {
            oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << word << " ";
        }
        else
        {
            std::string s(word);
            if (s.rfind("0x", 0) != 0 && s.rfind("0X", 0) != 0)
            {
                oss << "0x";
            }
            oss << s << " ";
        }
    };

    for (const auto &word : program.text_buffer)
    {
        appendWord(word);
    }
    return oss.str();
}

// parser::decoder stores Load offsets in .imm and Store offsets in .offset
int getMemOffset(const parsed_instruction &inst)
{
    return (inst.format == InstrFormat::S) ? inst.offset : inst.imm;
}

const std::unordered_map<std::string, Kites::ProcessorType> processorTypes = {
    {"rvss", Kites::ProcessorType::RVSS},
    {"rv5-nh-nf", Kites::ProcessorType::RV5Stage_NH_NF},
    {"rv5-h-nf", Kites::ProcessorType::RV5Stage_H_NF},
    {"rv5-nh-f", Kites::ProcessorType::RV5Stage_NH_F},
    {"rv5-h-f", Kites::ProcessorType::RV5Stage_H_F},
};

Kites::AssembledProgram maybeReorder(const Kites::AssembledProgram &prog, Kites::ProcessorType type)
{
    const bool fwd = (type == Kites::ProcessorType::RV5Stage_H_F);
    Kites::ReorderResult r = Kites::reorderWords(prog.text_buffer, fwd); // static counts
    std::cout << "Reorder: cycles " << r.cyclesBefore << " -> " << r.cyclesAfter << " (stalls "
              << r.stallsBefore << " -> " << r.stallsAfter << ")\n";
    return Kites::applyReordering(prog, r);
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    Kites::setupVmStateDirectory();

    std::string assemblyFile;
    std::string processorTypeStr = "rvss";
    std::vector<std::string> memDumpArgs;
    bool replRequested = false;
    bool profileMode = false;
    bool reorderDepsMode = false;
    bool reorder = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--vm")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --vm requires a value.\n";
                printUsage(argv[0]);
                return 1;
            }
            processorTypeStr = argv[++i];
        }
        else if (arg == "--mem-dump")
        {
            if (i + 2 >= argc)
            {
                std::cerr << "Error: --mem-dump requires <hex_addr> <row_count>.\n";
                printUsage(argv[0]);
                return 1;
            }
            memDumpArgs.push_back(argv[++i]);
            memDumpArgs.push_back(argv[++i]);
        }
        else if (arg == "--repl" || arg == "-i")
        {
            replRequested = true;
        }
        else if (arg == "--profile")
        {
            profileMode = true;
        }
        else if (arg == "--reorder-deps")
        {
            reorderDepsMode = true;
        }
        else if (arg == "--reorder")
        {
            reorder = true;
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << "Error: unknown argument '" << arg << "'.\n";
            printUsage(argv[0]);
            return 1;
        }
        else if (assemblyFile.empty())
        {
            assemblyFile = arg;
        }
        else
        {
            std::cerr << "Error: unexpected extra argument '" << arg << "'.\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    auto processorTypeIt = processorTypes.find(processorTypeStr);
    if (processorTypeIt == processorTypes.end())
    {
        std::cerr << "Error: unknown VM type '" << processorTypeStr << "'.\n";
        printUsage(argv[0]);
        return 1;
    }

    const auto ptype = processorTypeIt->second;
    if (reorder && ptype != Kites::ProcessorType::RV5Stage_H_NF &&
        ptype != Kites::ProcessorType::RV5Stage_H_F)
    {
        std::cerr << "Error: --reorder needs --vm rv5-h-nf or rv5-h-f.\n";
        return 1;
    }

    if (replRequested || assemblyFile.empty())
    {
        Kites::ProcessorManager manager(nullptr, processorTypeIt->second);
        manager.setStepDelay(0);

        Kites::command_handler::ReplState state;
        if (!assemblyFile.empty())
        {
            try
            {
                Kites::AssembledProgram program = Kites::assemble(assemblyFile);
                if (reorder)
                    program = maybeReorder(program, ptype);

                manager.loadProgram(program);
                state.currentProgram = program;
                state.programLoaded = true;
                std::cout << "Loaded '" << assemblyFile << "': " << program.text_buffer.size()
                          << " instruction word(s), " << program.data_buffer.size()
                          << " data item(s).\n";
            }
            catch (const std::exception &e)
            {
                std::cerr << "Assembly failed: " << e.what() << "\n";
            }
        }

        return Kites::RunRepl(manager, state);
    }

    Kites::AssembledProgram program;
    try
    {

        program = Kites::assemble(assemblyFile);

        if (reorder)
        {
            try
            {
                program = maybeReorder(program, ptype);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Reorder skipped: " << e.what() << "\n";
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Assembly failed: " << e.what() << std::endl;
        return 1;
    }

    Kites::ProcessorManager manager(nullptr, processorTypeIt->second);

    bool runFailed = false;
    bool runFinished = false;
    std::string runErrorMsg;
    QObject::connect(&manager, &Kites::ProcessorManager::runErrorSignal, &manager,
                     [&](const QString &message, int /*sourceLine*/)
                     {
                         runFailed = true;
                         runErrorMsg = message.toStdString();
                     });
    QObject::connect(&manager, &Kites::ProcessorManager::runFinishedSignal, &manager,
                     [&]()
                     {
                         runFinished = true;
                     });

    manager.setStepDelay(0);
    manager.loadProgram(program);

    if (profileMode)
    {
        // 1. Decode assembled machine words and get DAG blocks
        parser p(buildHexCodeText(program));
        std::vector<parsed_instruction> instrs = p.instructions();
        DAG my_dag(instrs, true);
        std::vector<std::vector<parsed_instruction>> blocks = my_dag.blocks();

        // 2. Map instruction index to block ID & initialize candidate L/S and S/S pairs
        std::unordered_map<int, int> block_of_instr;
        std::set<std::pair<int, int>> independent_pairs;
        std::unordered_map<int, parsed_instruction> inst_map;

        for (size_t b = 0; b < blocks.size(); b++)
        {
            std::vector<parsed_instruction> mem_insts;
            for (const auto &inst : blocks[b])
            {
                block_of_instr[inst.index] = static_cast<int>(b);
                inst_map[inst.index] = inst;
                if (inst.format == InstrFormat::L || inst.format == InstrFormat::S)
                {
                    mem_insts.push_back(inst);
                }
            }

            // Key (load, store) for L/S pairs and (min, max) for S/S pairs
            for (size_t i = 0; i < mem_insts.size(); ++i)
            {
                for (size_t j = i + 1; j < mem_insts.size(); ++j)
                {
                    const auto &m1 = mem_insts[i];
                    const auto &m2 = mem_insts[j];

                    if (m1.format == InstrFormat::S && m2.format == InstrFormat::S)
                    {
                        independent_pairs.insert({std::min(m1.index, m2.index),
                                                  std::max(m1.index, m2.index)});
                    }
                    else if (m1.format == InstrFormat::L && m2.format == InstrFormat::S)
                    {
                        independent_pairs.insert({m1.index, m2.index});
                    }
                    else if (m1.format == InstrFormat::S && m2.format == InstrFormat::L)
                    {
                        independent_pairs.insert({m2.index, m1.index});
                    }
                }
            }
        }

        // 3. Iteration-Aware Execution Loop
        std::unordered_map<int, std::vector<uint64_t>> instr_regist;
        uint64_t current_pc = manager.getProgramCounter();
        uint64_t start_pc = current_pc;
        bool is_running = !instrs.empty();
        size_t step_count = 0;
        const size_t max_steps = 70000;

        while (is_running && !runFailed)
        {
            int instr_index = static_cast<int>((current_pc - start_pc) / 4);
            if (instr_index < 0 || instr_index >= static_cast<int>(instrs.size()))
            {
                break;
            }

            if (++step_count > max_steps)
            {
                std::cerr << "Profiling stopped: exceeded " << max_steps
                          << " steps (start_pc=0x" << std::hex << start_pc
                          << ", current_pc=0x" << current_pc << std::dec
                          << ", instrs=" << instrs.size() << ").\n";
                break;
            }

            int current_block =
                block_of_instr.count(instr_index) ? block_of_instr[instr_index] : -1;

            // Log all 32 GPRs BEFORE execution to prevent base register overwrite issues
            instr_regist[instr_index] = manager.getRegisterFile()->GetGprValues();

            manager.step();

            uint64_t next_pc = manager.getProgramCounter();
            int next_index = static_cast<int>((next_pc - start_pc) / 4);

            // Detect exit syscall/ebreak or end-of-program PC wrap/reset
            const auto &cur_inst = instrs[instr_index];
            const auto &gprs = instr_regist[instr_index];
            bool is_exit_ecall = (cur_inst.inst == "00000000000000000000000001110011" &&
                                  gprs.size() > 17 && (gprs[17] == 10 || gprs[17] == 93));
            bool is_ebreak = (cur_inst.inst == "00000000000100000000000001110011");
            bool hit_halt_inst = is_exit_ecall || is_ebreak;

            bool is_sequential = (cur_inst.format == InstrFormat::R ||
                                  cur_inst.format == InstrFormat::I ||
                                  cur_inst.format == InstrFormat::L ||
                                  cur_inst.format == InstrFormat::S ||
                                  cur_inst.format == InstrFormat::U);
            bool sequential_wrapped = is_sequential && (next_pc != current_pc + 4);

            if (runFinished || hit_halt_inst || sequential_wrapped || next_pc == current_pc ||
                next_index < 0 || next_index >= static_cast<int>(instrs.size()))
            {
                is_running = false;
            }

            int next_block = (is_running && block_of_instr.count(next_index))
                                 ? block_of_instr[next_index]
                                 : -1;

            // Boundary Detection: block changed, jumped backwards (loop repeat), or execution ended
            if (current_block != next_block || next_pc <= current_pc || !is_running)
            {
                for (auto it = independent_pairs.begin(); it != independent_pairs.end();)
                {
                    int inst1 = it->first;
                    int inst2 = it->second;

                    if (block_of_instr[inst1] == current_block)
                    {
                        if (instr_regist.count(inst1) && instr_regist.count(inst2))
                        {
                            int rs1_1 = inst_map[inst1].rs1;
                            int rs1_2 = inst_map[inst2].rs1;

                            if (rs1_1 >= 0 && rs1_1 < 32 && rs1_2 >= 0 && rs1_2 < 32)
                            {
                                uint64_t addr1 =
                                    instr_regist[inst1][rs1_1] +
                                    static_cast<uint64_t>(
                                        static_cast<int64_t>(getMemOffset(inst_map[inst1])));
                                uint64_t addr2 =
                                    instr_regist[inst2][rs1_2] +
                                    static_cast<uint64_t>(
                                        static_cast<int64_t>(getMemOffset(inst_map[inst2])));

                                int64_t diff = std::abs(static_cast<int64_t>(addr1) -
                                                        static_cast<int64_t>(addr2));

                                if (diff < 8) // Memory overlap in this iteration
                                {
                                    it = independent_pairs.erase(it);
                                    continue;
                                }
                            }
                        }
                    }
                    ++it;
                }
                instr_regist.clear();
            }
            current_pc = next_pc;
        }

        // 4. Write surviving independent pairs to load_store_dep.txt
        std::ofstream dep_file("load_store_dep.txt");
        for (const auto &pair : independent_pairs)
        {
            dep_file << pair.first << " " << pair.second << "\n";
        }
        dep_file.close();
        std::cout << "Profiling complete. Independent pairs exported to load_store_dep.txt\n";
    }
    else if (reorderDepsMode)
    {
        // 1. Decode assembled machine words and initialize DAG
        parser p(buildHexCodeText(program));
        std::vector<parsed_instruction> instrs = p.instructions();
        DAG my_dag(instrs, true);

        // 2. Load non-overlapping pairs from load_store_dep.txt (preserving (load, store) order)
        std::ifstream in_dep("load_store_dep.txt");
        std::set<std::pair<int, int>> independent_pairs;
        int idx1, idx2;
        while (in_dep >> idx1 >> idx2)
        {
            independent_pairs.insert({idx1, idx2});
        }
        // Uncomment once set_independent_mem_pairs is added to reordering/DAG.h:
        // my_dag.set_independent_mem_pairs(independent_pairs);

        // 3. Schedule and output reordered instructions
        std::vector<std::vector<parsed_instruction>> blocks = my_dag.blocks();
        auto dep_matrix = my_dag.depend_matrix_full();
        scheduler sched(blocks, dep_matrix, true);

        int cycles_before = 0, cycles_after = 0;
        std::vector<parsed_instruction> reordered =
            sched.code_scheduler(&cycles_before, &cycles_after);

        std::cout << "Reordering complete. Cycles Before: " << cycles_before
                  << " | Cycles After: " << cycles_after << "\n"
                  << "Reordered Instructions:\n";
        for (const auto &inst : reordered)
        {
            std::cout << "  " << inst.inst << "\n";
        }
    }
    else
    {
        manager.run();
    }

    if (runFailed)
    {
        std::cerr << "Run failed: " << runErrorMsg << std::endl;
    }

    Kites::DumpRegisters(Kites::globals::registers_dump_file_path, *manager.getRegisterFile());

    if (!memDumpArgs.empty())
    {
        try
        {
            manager.getMemoryController()->dumpMemory(memDumpArgs);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Memory dump failed: " << e.what() << std::endl;
            return 3;
        }
    }

    std::cout << "Assembled '" << assemblyFile << "': " << program.text_buffer.size()
              << " instruction word(s), " << program.data_buffer.size() << " data item(s).\n"
              << "Run " << (runFailed ? "failed" : "completed") << ".\n"
              << "Registers   -> " << Kites::globals::registers_dump_file_path.string() << "\n"
              << "Disassembly -> " << Kites::globals::disassembly_file_path.string() << "\n"
              << "Errors      -> " << Kites::globals::errors_dump_file_path.string();
    if (!memDumpArgs.empty())
    {
        std::cout << "\nMemory      -> " << Kites::globals::memory_dump_file_path.string();
    }
    std::cout << std::endl;

    return runFailed ? 2 : 0;
}