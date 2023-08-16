#ifndef __LOOPUNROLL_H__
#define __LOOPUNROLL_H__

#include "Unit.h"
#include "LoopAnalyzer.h"
#include <queue>
#include <stack>

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
    void InitLoopOp(Operand *begin, Operand *stride, Operand *end, Operand *op1, Operand *op2, BasicBlock *bb);
    BasicBlock *LastBasicBlock(Operand *instr, BasicBlock *bb);
};

#endif