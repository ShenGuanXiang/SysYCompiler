#ifndef __AUTOINLINE_H__
#define __AUTOINLINE_H__

#include "Unit.h"
#include "DeadCodeElim.h"

/*

*/
class AutoInliner
{
private:
    Unit *unit;
    std::vector<Function *> removed_func;
    std::unordered_map<Function *, std::set<Function *>> calls;
    std::unordered_map<Function *, std::vector<Function *>> called;

public:
    AutoInliner(Unit *unit) : unit(unit){};
    void pass();
    void pass(Instruction *instr);
    void CallIntrNum();
    void RecurDetect();
    void UpdateRecur(Function *, std::set<Function *> &);
    bool ShouldBeinlined(Function *f);
};

#endif