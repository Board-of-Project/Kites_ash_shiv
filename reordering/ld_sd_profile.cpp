#include "ld_sd_profile.h"

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
