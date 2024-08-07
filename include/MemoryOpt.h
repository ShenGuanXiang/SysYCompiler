#ifndef __MEMORYOPT_H__
#define __MEMORYOPT_H__

#include "Unit.h"

// 访存优化

std::pair<SymbolEntry *, int> analyzeGep(Instruction *inst);


class MemoryOpt
{
private:
    Unit *unit;

public:
    MemoryOpt(Unit *unit) : unit(unit){};
    void pass();
    void pass(Function *);
};

#endif