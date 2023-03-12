#ifndef __MEM2REG_H__
#define __MEM2REG_H__

#include "Unit.h"

/*
    Mem2Reg for IR:
        1) 计算Idom
        2) 计算DF
        3) insertPHI (pruned SSA)
        4) Rename
*/
class Mem2Reg
{
private:
    Unit *unit;
    void InsertPhi(Function *);
    void Rename(Function *);

public:
    Mem2Reg(Unit *unit) : unit(unit){};
    void pass();
};

#endif