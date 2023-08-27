#ifndef __AUTOINLINE_H__
#define __AUTOINLINE_H__

#include "Unit.h"
#include "DeadCodeElim.h"
#include <queue>

/*

*/
class AutoInliner
{
private:
    Unit *unit;
    std::map<Function *, bool> is_recur;
    std::map<Function *, int> degree;
    const int MAXINLINEITER = 2, MAXRECURCALL = 10;

public:
    AutoInliner(Unit *unit) : unit(unit){};
    void pass(bool recur_line = true);
    void pass(Instruction *instr, Function *func = nullptr);
    void RecurDetect();
    void UpdateRecur(Function *, std::set<Function *> &);
    bool ShouldBeinlined(Function *f);
    void InitDegree();
    void Print_Funcinline(std::queue<Function *> &);
    void RecurInline(Function *);
    Function *deepCopy(Function *);
    void ReplSingleUseWith(Instruction *inst, int i, Operand *new_op);
    void ClearRedundantParams();
};

#endif