#pragma once
#include "cycle_report.h"
#include <cstdint>
#include <string>
#include <vector>

struct TestCase
{
    std::vector<uint32_t> words;     // original program (machine code)
    bool forwarding = false;         // processor used
    std::vector<long long> counts;   // executions per instruction number (empty = unknown)
    bool hasMeasured = false;
    bool ranReordered = false;       // which version the measurement belongs to
    MeasuredStats measured;
};

bool writeTestCase(const std::string &path, const TestCase &tc);
bool readTestCase(const std::string &path, TestCase &tc);