#pragma once

struct MeasuredStats               // numbers read from a real Kites run
{
    long long cycles = 0;          // ProcessorManager::getCycles()
    long long retired = 0;         // getInstructionsRetired()
    long long dataStalls = 0;      // getTotalStallCycles()
};

// cycles = retired + dataStalls + other   (other = pipeline fill + branch/jump bubbles)
inline long long otherCycles(const MeasuredStats &m) { return m.cycles - m.retired - m.dataStalls; }

// Fill and control bubbles don't change when instructions move inside a block, so one measured
// run plus the model's stall difference predicts the other version's total.
inline long long predictOtherCycles(const MeasuredStats &ran, long long modelStallsRan,
                                    long long modelStallsOther)
{
    return ran.cycles - modelStallsRan + modelStallsOther;
}