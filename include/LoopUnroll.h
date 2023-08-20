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
    int MAXUNROLLNUM=400;
    int UNROLLNUM=4;
    int unrolling_factor;
    std::set<Loop *> Loops;

public:
    LoopUnroll(Unit *u) : unit(u){};
    void pass();
    std::vector<Loop *> FindCandidateLoop();
    void Unroll(Loop *);
    Operand* getBeginOp(BasicBlock* bb,Operand* strideOp,std::stack<Instruction*>& Insstack);
    bool isRegionConst(Operand* i, Operand* c);
    void specialCopyInstructions(BasicBlock* bb,int num,Operand* endOp,Operand* strideOp,bool ifall);
    void normalCopyInstructions(BasicBlock* condbb,BasicBlock* bodybb,Operand* beginOp,Operand* endOp,Operand* strideOp);
};

#endif