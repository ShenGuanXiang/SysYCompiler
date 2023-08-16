#ifndef __LOOPUNROLL_H__
#define __LOOPUNROLL_H__

#include "Unit.h"
#include <queue>
#include "LoopAnalyzer.h"

class LoopUnroll
{
private:
    Unit *unit;
    const int MAX_UNROLLING_FACTOR = 4;
    const int MIN_UNROLLING_FACTOR = 2;
    int unrolling_factor;
    std::set<Loop *> Loops;

public:
    LoopUnroll(Unit *u) : unit(u){};
    void pass();
    std::vector<Loop *> FindCandidateLoop();
    void Unroll(Loop *);
};

#endif