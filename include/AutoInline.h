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
    std::vector<Function *> removed_func;
    
public:
    AutoInliner(Unit *unit) : unit(unit){};
    void pass();
    void pass(Instruction *instr);
    void RecurDetect();
    void UpdateRecur(Function *, std::set<Function *> &);
    bool ShouldBeinlined(Function *f);
    void Print_Funcinline(std::queue<Function *>&);
};

#endif