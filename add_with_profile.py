#!/usr/bin/env python3
"""Adds `--reorder --with-profile` to kites-cli.

Run from the repo root, AFTER the merge of feature/get_cycles is committed
(it expects the resolved cli/main.cpp with `reorder` / `reorderDepsMode`).
Every edit asserts it matched exactly once, so a mismatch fails loudly.
"""


def edit(path, pairs):
    s = open(path).read()
    for old, new in pairs:
        assert s.count(old) == 1, f"{path}: expected exactly 1 match for: {old[:60]!r}, got {s.count(old)}"
        s = s.replace(old, new)
    open(path, 'w').write(s)


# ---------- DAG.h ----------
edit('reordering/DAG.h', [(
"    explicit DAG(std::vector<parsed_instruction> &temp, bool forw);",
"""    // indepPairs: optional (index, index) load/store pairs known to be independent.
    // If given, they are used directly and no profile file is read.
    // If null, the pairs are read from $KITES_LD_SD_FILE, or ../load_store_dep.txt.
    explicit DAG(std::vector<parsed_instruction> &temp, bool forw,
                 const std::vector<std::pair<int, int>> *indepPairs = nullptr);""")])

# ---------- DAG.cpp ----------
edit('reordering/DAG.cpp', [
("#include <cstdio>\n#include <vector>\n", "#include <cstdio>\n#include <cstdlib>\n#include <string>\n#include <vector>\n"),
("DAG::DAG(vector<parsed_instruction> &temp, bool forw)\n",
 "DAG::DAG(vector<parsed_instruction> &temp, bool forw,\n         const std::vector<std::pair<int, int>> *indepPairs)\n"),
("""    // pairs of load/store instructions that were profiled as independent
    FILE* fptr = fopen("../load_store_dep.txt" , "r");
    if (!fptr)
    {
        fprintf(stderr, "[DAG] cannot open ../load_store_dep.txt - keeping all load/store pairs dependent\\n");
        return;
    }
""",
"""    // pairs supplied directly by the caller (e.g. kites-cli --reorder --with-profile)
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
        fprintf(stderr, "[DAG] cannot open %s - keeping all load/store pairs dependent\\n", depPath.c_str());
        return;
    }
""")])

# ---------- reorder_core.h / .cpp ----------
edit('reordering/reorder_core.h', [
("#include <cstdint>\n#include <vector>\n", "#include <cstdint>\n#include <utility>\n#include <vector>\n"),
("                           const std::vector<long long> &instrCounts = {});",
 "                           const std::vector<long long> &instrCounts = {},\n                           const std::vector<std::pair<int, int>> *indepPairs = nullptr);")])
edit('reordering/reorder_core.cpp', [
("ReorderResult reorderWords(const std::vector<uint32_t> &text, bool forwarding,\n                           const std::vector<long long> &instrCounts)\n",
 "ReorderResult reorderWords(const std::vector<uint32_t> &text, bool forwarding,\n                           const std::vector<long long> &instrCounts,\n                           const std::vector<std::pair<int, int>> *indepPairs)\n"),
("    DAG dag(instrs, forwarding);", "    DAG dag(instrs, forwarding, indepPairs);")])

# ---------- cli/main.cpp ----------
p = 'cli/main.cpp'
s = open(p).read()

# 1. extract the profiling body (steps 1-3) out of main()
start_marker = "    if (profileMode)\n    {\n"
end_marker = "        // 4. Write surviving independent pairs to load_store_dep.txt\n"
a = s.index(start_marker) + len(start_marker)
b = s.index(end_marker)
assert s.count(start_marker) == 1 and s.count(end_marker) == 1
body = s[a:b]
body_dedented = "\n".join((l[4:] if l.startswith("    ") else l) for l in body.split("\n"))

# 2. in main(): replace the body with a call to the new helper
s = s[:a] + "        const std::set<std::pair<int, int>> independent_pairs =\n            profileLoop(manager, program, runFailed, runFinished);\n\n" + s[b:]

# 3. new helpers, inserted before maybeReorder
helpers = '''// Runs the (already loaded) program step by step on a single-cycle VM and returns the
// (load, store) / (store, store) instruction-index pairs that never touched overlapping memory.
std::set<std::pair<int, int>> profileLoop(Kites::ProcessorManager &manager,
                                          const Kites::AssembledProgram &program,
                                          const bool &runFailed, const bool &runFinished)
{
''' + body_dedented.rstrip() + '''

    return independent_pairs;
}

// Profiles on a fresh single-cycle (rvss) VM. rvss is required because the profiler maps
// PC -> instruction 1:1, which a stalling pipeline would break.
std::set<std::pair<int, int>> profileProgram(const Kites::AssembledProgram &program)
{
    Kites::ProcessorManager manager(nullptr, Kites::ProcessorType::RVSS);
    bool runFailed = false;
    bool runFinished = false;
    QObject::connect(&manager, &Kites::ProcessorManager::runErrorSignal, &manager,
                     [&](const QString &, int) { runFailed = true; });
    QObject::connect(&manager, &Kites::ProcessorManager::runFinishedSignal, &manager,
                     [&]() { runFinished = true; });
    manager.setStepDelay(0);
    manager.loadProgram(program);
    return profileLoop(manager, program, runFailed, runFinished);
}

'''
old_mr = "Kites::AssembledProgram maybeReorder(const Kites::AssembledProgram &prog, Kites::ProcessorType type)\n{\n    const bool fwd = (type == Kites::ProcessorType::RV5Stage_H_F);\n    Kites::ReorderResult r = Kites::reorderWords(prog.text_buffer, fwd); // static counts\n"
new_mr = '''Kites::AssembledProgram maybeReorder(const Kites::AssembledProgram &prog, Kites::ProcessorType type,
                                     bool withProfile = false)
{
    const bool fwd = (type == Kites::ProcessorType::RV5Stage_H_F);
    std::vector<std::pair<int, int>> pairs;
    const std::vector<std::pair<int, int>> *pairsPtr = nullptr;
    if (withProfile)
    {
        const auto found = profileProgram(prog);
        pairs.assign(found.begin(), found.end());
        pairsPtr = &pairs;
        std::cout << "Profile: " << pairs.size() << " independent load/store pair(s) found.\\n";
    }
    Kites::ReorderResult r = Kites::reorderWords(prog.text_buffer, fwd, {}, pairsPtr); // static counts
'''
assert s.count(old_mr) == 1
s = s.replace(old_mr, helpers + new_mr)


# 4. flag, validation, call sites, usage, --profile output path
def sub(old, new, count=1):
    global s
    assert s.count(old) == count, f"main.cpp: expected {count} of {old[:50]!r}, got {s.count(old)}"
    s = s.replace(old, new)


sub("    bool reorder = false;\n", "    bool reorder = false;\n    bool withProfile = false;\n")
sub('''        else if (arg == "--reorder")
        {
            reorder = true;
        }
''', '''        else if (arg == "--reorder")
        {
            reorder = true;
        }
        else if (arg == "--with-profile")
        {
            withProfile = true;
        }
''')
sub('''    const auto ptype = processorTypeIt->second;
''', '''    if (withProfile && !reorder)
    {
        std::cerr << "Error: --with-profile only works together with --reorder.\\n";
        return 1;
    }

    const auto ptype = processorTypeIt->second;
''')
sub("maybeReorder(program, ptype);", "maybeReorder(program, ptype, withProfile);", count=2)
sub('''        << " [--reorder] [--profile|--reorder-deps]\\n"''', '''        << " [--reorder [--with-profile]] [--profile|--reorder-deps]\\n"''')
sub('''        << "                  running (only with rv5-h-nf / rv5-h-f)\\n"''',
    '''        << "                  running (only with rv5-h-nf / rv5-h-f)\\n"
        << "  --with-profile  with --reorder: first profile the program (on rvss) to find\\n"
        << "                  independent load/store pairs, then reorder using them\\n"''')
sub('''        << "                  pairs and export them to load_store_dep.txt\\n"''',
    '''        << "                  pairs and export them to load_store_dep.txt\\n"
        << "                  (or the file named by $KITES_LD_SD_FILE)\\n"''')
sub('''        std::ofstream dep_file("load_store_dep.txt");''',
    '''        const char *depEnv = std::getenv("KITES_LD_SD_FILE");
        const std::string depPath = (depEnv && *depEnv) ? depEnv : "load_store_dep.txt";
        std::ofstream dep_file(depPath);''')
sub('''std::cout << "Profiling complete. Independent pairs exported to load_store_dep.txt\\n";''',
    '''std::cout << "Profiling complete. Independent pairs exported to " << depPath << "\\n";''')
open(p, 'w').write(s)
print("edits applied")
