#ifndef __LOOPUNROLL_H__
#define __LOOPUNROLL_H__

#include "Unit.h"
#include "LoopAnalyzer.h"
#include <queue>
#include <stack>
#include "SimplifyCFG.h"

struct LoopInfo
{
    Instruction *cmp, *phi;
    std::pair<Operand *, Operand *> indvar_range; // [first,second)
    std::set<BasicBlock *> loop_blocks;
    BasicBlock *loop_header;
    BasicBlock *loop_exiting_block;
};

class LoopUnroll
{
private:
    Unit *unit;
    const int MAXUNROLLNUM = 200;
    const int UNROLLNUM = 4;
    std::set<Loop *> Loops;

public:
    LoopUnroll(Unit *u) : unit(u){};
    void pass();
    Loop *FindCandidateLoop();
    void Unroll(Loop *);
    void specialCopyInstructions(BasicBlock *bb, int num, Operand *endOp, Operand *strideOp, bool ifall);
    void normalCopyInstructions(BasicBlock *condbb, BasicBlock *bodybb, Operand *beginOp, Operand *endOp, Operand *strideOp);
};

#endif