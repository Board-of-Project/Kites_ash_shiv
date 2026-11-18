#include "cycle_report.h"
#include "reorder_core.h"
#include "testcase.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace Kites;

static const char *SAMPLE =
    "0x10000093 0x20000113 0x0000a183 0x0040a203 0x004182b3 0x0080a303 0x405303b3 0x00c0a403 "
    "0x003444b3 0x00748513 0x008575b3 0x0075e633 0x003616b3 0x0046d713 0x00e12023 0x00c707b3 "
    "0x00f3a833 0x00f12223 0x06480893 0x41088933 0x00391993 0x01312423 0x0100aa03 0x013a0ab3 "
    "0x0ffafb13 0x100b6b93 0x015bcc33 0x01812623 0x402c5c93 0x018cbd33 0x01ac8db3 0x417d8e33 "
    "0x01c12823 0x0d1e0e93 0x01beef33 0x01e12a23 ";

static void report(const TestCase &tc, bool forwarding)
{
    ReorderResult r = reorderWords(tc.words, forwarding, tc.counts);
    printf("== %s, %s ==\n", forwarding ? "with forwarding" : "no forwarding",
           r.weighted ? "weighted by executed counts" : "one pass (loops counted once)");
    printf("instructions executed (model): %d\n", r.instrCount);
    printf("model data stalls            : %d -> %d\n", r.stallsBefore, r.stallsAfter);
    printf("model cycles (no fill/ctrl)  : %d -> %d\n", r.cyclesBefore, r.cyclesAfter);
    printf("new order (original indices) : ");
    for (int i : r.origIndex)
        printf("%d ", i);
    printf("\n");

    if (!tc.hasMeasured)
        return;
    const MeasuredStats &m = tc.measured;
    printf("\nmeasured by Kites (%s run): cycles=%lld retired=%lld data_stalls=%lld other=%lld\n",
           tc.ranReordered ? "reordered" : "original", m.cycles, m.retired, m.dataStalls,
           otherCycles(m));
    const long long modelRan = tc.ranReordered ? r.stallsAfter : r.stallsBefore;
    const long long modelOther = tc.ranReordered ? r.stallsBefore : r.stallsAfter;
    printf("model check: model stalls %lld vs measured %lld (difference %lld)\n", modelRan,
           m.dataStalls, m.dataStalls - modelRan);
    printf("predicted %s total: %lld cycles\n", tc.ranReordered ? "original" : "reordered",
           predictOtherCycles(m, modelRan, modelOther));
}

int main(int argc, char **argv)
{
    TestCase tc;
    if (argc > 1)
    {
        if (!readTestCase(argv[1], tc))
        {
            fprintf(stderr, "cannot read test case: %s\n", argv[1]);
            return 1;
        }
        report(tc, tc.forwarding);
    }
    else
    {
        std::istringstream in(SAMPLE);
        std::string t;
        while (in >> t)
            tc.words.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16)));
        report(tc, false);
        report(tc, true);
    }
    return 0;
}