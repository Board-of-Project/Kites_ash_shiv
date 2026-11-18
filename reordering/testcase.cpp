#include "testcase.h"
#include <cstdio>
#include <fstream>
#include <sstream>

bool writeTestCase(const std::string &path, const TestCase &tc)
{
    std::ofstream f(path);
    if (!f)
        return false;
    f << "forwarding: " << (tc.forwarding ? 1 : 0) << "\nwords:";
    for (uint32_t w : tc.words)
    {
        char b[16];
        snprintf(b, sizeof b, " 0x%08x", w);
        f << b;
    }
    f << "\ncounts:";
    for (long long c : tc.counts)
        f << ' ' << c;
    f << "\n";
    if (tc.hasMeasured)
    {
        f << "ran: " << (tc.ranReordered ? "reordered" : "original") << "\n"
          << "cycles: " << tc.measured.cycles << "\n"
          << "retired: " << tc.measured.retired << "\n"
          << "data_stalls: " << tc.measured.dataStalls << "\n";
    }
    return static_cast<bool>(f);
}

bool readTestCase(const std::string &path, TestCase &tc)
{
    std::ifstream f(path);
    if (!f)
        return false;
    tc = TestCase{};
    std::string line;
    while (std::getline(f, line))
    {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = line.substr(0, colon);
        std::istringstream rest(line.substr(colon + 1));
        if (key == "forwarding") { int v = 0; rest >> v; tc.forwarding = (v != 0); }
        else if (key == "words") { std::string t; while (rest >> t) tc.words.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16))); }
        else if (key == "counts") { long long c; while (rest >> c) tc.counts.push_back(c); }
        else if (key == "ran") { std::string v; rest >> v; tc.ranReordered = (v == "reordered"); tc.hasMeasured = true; }
        else if (key == "cycles") { rest >> tc.measured.cycles; tc.hasMeasured = true; }
        else if (key == "retired") { rest >> tc.measured.retired; }
        else if (key == "data_stalls") { rest >> tc.measured.dataStalls; }
    }
    return !tc.words.empty();
}