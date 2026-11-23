#ifndef LD_SD_PROFILE_H
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
