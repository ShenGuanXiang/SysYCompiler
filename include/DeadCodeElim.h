#ifndef __DEADCODEELIM_H__
#define __DEADCODEELIM_H__

#include "Unit.h"
#include "SimplifyCFG.h"

/*
    Dead Instr Elimination for IR:
        1) 将重要的指令标记
        2) 把重要的指令和所有相关的指令标记
        3) 清除所有的未标记指令
        4) 删除无前导非入口块
*/
class DeadCodeElim
{
private:
    Unit *unit;

public:
    DeadCodeElim(Unit *unit) : unit(unit){};
    void DeadInstrMark(Function *f);
    void DeadInstrEliminate(Function *f);
    void pass(Function *func);
    void pass();
    void DeleteUselessFunc();
};

/*
    Dead Instr Elimination for MachineCode:
*/
class MachineDeadCodeElim
{
private:
    MachineUnit *unit;

public:
    MachineDeadCodeElim(MachineUnit *unit) : unit(unit){};
    void pass();
    void pass(MachineFunction *f);
    void SingleBrDelete(MachineFunction *f);
};

#endif