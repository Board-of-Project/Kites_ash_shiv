#!/usr/bin/env python3
"""GUI memory-dependency reordering via `kites-cli --profile`.

Run from the repo root AFTER add_with_profile.py has been applied.
 - adds reordering/ld_sd_profile.h / .cpp  (runs kites-cli --profile, returns the ld/sd pairs)
 - reorderWords(): when no pairs are supplied (the GUI case) it calls that helper and passes
   the pairs to DAG, so the scheduler handles memory + register dependencies together
 - kites-cli: new --profile-out <file>, prints KITES_PROFILE_HASH, never auto-profiles itself
 - reordering/Makefile: standalone `make` keeps building
No GUI source file is changed. Every edit asserts it matched exactly once.
"""
import os

NEW_FILES = {
    'reordering/ld_sd_profile.h': r"""#ifndef LD_SD_PROFILE_H
#define LD_SD_PROFILE_H

// Obtains the independent load/store pairs by running `kites-cli --profile` as a child
// process, so callers that only have machine words (e.g. the GUI) get memory-dependency
// reordering without any profiling code of their own.

#include <cstdint>
#include <utility>
#include <vector>

namespace ldsd
{

// FNV-1a over the little-endian bytes of the instruction words. kites-cli prints this for
// the program it profiled, so the caller can verify the pairs belong to ITS program.
inline uint64_t hashWords(const std::vector<uint32_t> &words)
{
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t w : words)
        for (int b = 0; b < 4; ++b)
        {
            h ^= static_cast<uint8_t>(w >> (8 * b));
            h *= 1099511628211ULL;
        }
    return h;
}

// Automatic profiling is ON by default (the GUI relies on that). kites-cli turns it off for
// itself because it has its own --with-profile. Also disabled by env KITES_NO_AUTO_PROFILE.
void setAutoProfile(bool enabled);
bool autoProfileEnabled();

// Profiles the program currently in <data dir>/vm_state/temp.asm (what the GUI just wrote)
// by running `kites-cli <copy of temp.asm> --vm rvss --profile --profile-out <tmp>`.
// The CLI is looked up via env KITES_CLI, else `kites-cli` next to the running executable.
// Returns true and fills outPairs only if the CLI ran successfully AND reported a program
// hash equal to hashWords(words). Returns false (callers fall back) otherwise.
// Results are cached per program, so re-running an unchanged program does not respawn it.
bool profileViaCli(const std::vector<uint32_t> &words, std::vector<std::pair<int, int>> &outPairs);

} // namespace ldsd

#endif
""",
    'reordering/ld_sd_profile.cpp': r"""#include "ld_sd_profile.h"

#include "../src/common/globals.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define KITES_POPEN _popen
#define KITES_PCLOSE _pclose
#define KITES_GETPID _getpid
#else
#include <unistd.h>
#define KITES_POPEN popen
#define KITES_PCLOSE pclose
#define KITES_GETPID getpid
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace ldsd
{
namespace
{
namespace fs = std::filesystem;

std::atomic<bool> g_autoProfile{true};

uint64_t fnvBytes(const std::string &s)
{
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string readFile(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path executableDir()
{
    std::error_code ec;
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return fs::path(std::string(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return fs::weakly_canonical(fs::path(buf), ec).parent_path();
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return p.parent_path();
#endif
    return {};
}

fs::path findCli()
{
    std::error_code ec;
    if (const char *e = std::getenv("KITES_CLI"); e && *e)
    {
        fs::path p(e);
        if (fs::is_regular_file(p, ec))
            return p;
    }
#ifdef _WIN32
    const char *name = "kites-cli.exe";
#else
    const char *name = "kites-cli";
#endif
    const fs::path dir = executableDir();
    if (!dir.empty())
    {
        fs::path p = dir / name;
        if (fs::is_regular_file(p, ec))
            return p;
    }
    return {};
}

std::string quote(const std::string &s)
{
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
#endif
}

std::mutex g_cacheMutex;
std::map<uint64_t, std::vector<std::pair<int, int>>> g_cache;
std::atomic<unsigned> g_counter{0};

} // namespace

void setAutoProfile(bool enabled)
{
    g_autoProfile = enabled;
}

bool autoProfileEnabled()
{
    if (const char *e = std::getenv("KITES_NO_AUTO_PROFILE"); e && *e)
        return false;
    return g_autoProfile;
}

bool profileViaCli(const std::vector<uint32_t> &words, std::vector<std::pair<int, int>> &outPairs)
{
    std::error_code ec;

    // the program the GUI just assembled
    const fs::path srcAsm = Kites::globals::temporary_assembly_file_path;
    if (!fs::is_regular_file(srcAsm, ec))
        return false;
    const std::string asmText = readFile(srcAsm);
    if (asmText.empty())
        return false;

    const uint64_t wordsHash = hashWords(words);
    const uint64_t key = wordsHash * 1315423911ULL ^ fnvBytes(asmText);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_cache.find(key);
        if (it != g_cache.end())
        {
            outPairs = it->second;
            return true;
        }
    }

    const fs::path cli = findCli();
    if (cli.empty())
        return false; // no CLI available: caller falls back to the profile-file route

    // private copies: the CLI never touches the GUI's own temp.asm
    const fs::path tmpDir = fs::temp_directory_path(ec);
    if (ec)
        return false;
    const std::string stem = "kites_ldsd_" + std::to_string(KITES_GETPID()) + "_" +
                             std::to_string(g_counter.fetch_add(1));
    const fs::path tmpAsm = tmpDir / (stem + ".asm");
    const fs::path tmpPairs = tmpDir / (stem + ".pairs");
    {
        std::ofstream out(tmpAsm, std::ios::binary);
        out << asmText;
        if (!out)
            return false;
    }

#ifdef _WIN32
    const std::string devnullIn = " <nul";
    const std::string devnullErr = " 2>nul";
#else
    const std::string devnullIn = " </dev/null";
    const std::string devnullErr = " 2>/dev/null";
#endif
    std::string cmd = quote(cli.string()) + " " + quote(tmpAsm.string()) + " --vm rvss --profile --profile-out " +
                      quote(tmpPairs.string()) + devnullIn + devnullErr;
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif

    std::string output;
    int status = -1;
    if (FILE *pipe = KITES_POPEN(cmd.c_str(), "r"))
    {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe))
            output += buf;
        status = KITES_PCLOSE(pipe);
    }

    bool ok = (status == 0);
    std::vector<std::pair<int, int>> pairs;
    if (ok)
    {
        // verify the CLI profiled the same program we are about to reorder
        bool hashSeen = false;
        std::istringstream lines(output);
        std::string line;
        const std::string tag = "KITES_PROFILE_HASH ";
        while (std::getline(lines, line))
        {
            if (line.compare(0, tag.size(), tag) == 0)
            {
                try
                {
                    hashSeen = (std::stoull(line.substr(tag.size()), nullptr, 16) == wordsHash);
                }
                catch (...)
                {
                    hashSeen = false;
                }
            }
        }
        ok = hashSeen;
        if (!ok)
            std::fprintf(stderr, "[ldsd] kites-cli profiled a different program - ignoring its pairs\n");
    }
    else
    {
        std::fprintf(stderr, "[ldsd] kites-cli --profile failed (status %d) - falling back\n", status);
    }

    if (ok)
    {
        std::ifstream in(tmpPairs);
        int a, b;
        while (in >> a >> b)
            pairs.emplace_back(a, b);
    }

    fs::remove(tmpAsm, ec);
    fs::remove(tmpPairs, ec);

    if (!ok)
        return false;

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (g_cache.size() > 64)
            g_cache.clear();
        g_cache[key] = pairs;
    }
    outPairs = std::move(pairs);
    return true;
}

} // namespace ldsd
""",
}


def edit(path, pairs):
    s = open(path).read()
    for old, new in pairs:
        assert s.count(old) == 1, f"{path}: expected exactly 1 match for: {old[:70]!r}, got {s.count(old)}"
        s = s.replace(old, new)
    open(path, 'w').write(s)


# refuse to run twice / on the wrong base, before touching anything
assert not os.path.exists('reordering/ld_sd_profile.cpp'), "already applied (reordering/ld_sd_profile.cpp exists)"
assert 'withProfile' in open('cli/main.cpp').read(), "apply add_with_profile.py first"

edit('reordering/reorder_core.cpp', [
('#include "reorder_core.h"\n', '#include "reorder_core.h"\n#include "ld_sd_profile.h"\n'),
("    DAG dag(instrs, forwarding, indepPairs);",
"""    // No pairs supplied (e.g. the GUI): get them by running `kites-cli --profile`.
    // On any failure, indepPairs stays null and DAG falls back to the profile file.
    std::vector<std::pair<int, int>> autoPairs;
    if (!indepPairs && ldsd::autoProfileEnabled() && ldsd::profileViaCli(text, autoPairs))
        indepPairs = &autoPairs;

    DAG dag(instrs, forwarding, indepPairs);"""),
])

edit('reordering/Makefile', [(
"SRCS := parser.cpp DAG.cpp heurestic.cpp scheduler.cpp reorder_core.cpp testcase.cpp tester.cpp",
"SRCS := parser.cpp DAG.cpp heurestic.cpp scheduler.cpp reorder_core.cpp ld_sd_profile.cpp ../src/common/globals.cpp testcase.cpp tester.cpp")])

edit('cli/main.cpp', [
('#include "../reordering/DAG.h"\n', '#include "../reordering/DAG.h"\n#include "../reordering/ld_sd_profile.h"\n'),
("int main(int argc, char *argv[])\n{\n", "int main(int argc, char *argv[])\n{\n    ldsd::setAutoProfile(false); // the CLI has its own --with-profile; never spawn itself\n"),
("    bool withProfile = false;\n", "    bool withProfile = false;\n    std::string profileOutPath;\n"),
('''        else if (arg == "--with-profile")
        {
            withProfile = true;
        }
''', '''        else if (arg == "--with-profile")
        {
            withProfile = true;
        }
        else if (arg == "--profile-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --profile-out requires a file path.\\n";
                printUsage(argv[0]);
                return 1;
            }
            profileOutPath = argv[++i];
        }
'''),
('''    if (withProfile && !reorder)
    {''', '''    if (!profileOutPath.empty() && !profileMode)
    {
        std::cerr << "Error: --profile-out only works together with --profile.\\n";
        return 1;
    }

    if (withProfile && !reorder)
    {'''),
('''        const std::string depPath = (depEnv && *depEnv) ? depEnv : "load_store_dep.txt";''',
 '''        const std::string depPath = !profileOutPath.empty() ? profileOutPath
                                    : ((depEnv && *depEnv) ? std::string(depEnv) : std::string("load_store_dep.txt"));'''),
('''        std::cout << "Profiling complete. Independent pairs exported to " << depPath << "\\n";''',
 '''        std::cout << "Profiling complete. Independent pairs exported to " << depPath << "\\n";
        // lets callers (reordering/ld_sd_profile.cpp) verify the pairs belong to their program
        std::cout << "KITES_PROFILE_HASH " << std::hex << ldsd::hashWords(program.text_buffer) << std::dec << "\\n";'''),
('''        << "                  (or the file named by $KITES_LD_SD_FILE)\\n"''',
 '''        << "                  (or --profile-out <file>, or the file named by $KITES_LD_SD_FILE)\\n"'''),
])

for path, text in NEW_FILES.items():
    with open(path, 'w') as f:
        f.write(text)
print("edits applied; new files:", ", ".join(NEW_FILES))
