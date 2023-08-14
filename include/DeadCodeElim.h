#ifndef __DEADCODEELIM_H__
#define __DEADCODEELIM_H__

#include "Unit.h"
#include "SimplifyCFG.h"

/*
    Dead Instr Elimination for IR:
        1) 将重要的指令标记
        2) 把重要的指令和所有相关的指令标记
        3) 清除所有的未标记指令
        4) SimplifyCFG
*/
class DeadCodeElim
{
private:
    Unit *unit;
    std::map<Instruction *, bool> instDCEMarked;
    std::map<BasicBlock *, bool> bbDCEMarked;

public:
    DeadCodeElim(Unit *unit) : unit(unit){};
    void deadInstrMark(Function *f);
    bool deadInstrEliminate(Function *f);
    BasicBlock* get_nearest_dom(Instruction *instr);
    void pass(Function *func);
    void pass();
    void deleteUselessFunc();
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
    void pass(bool iter = true);
    void pass(MachineFunction *f, bool iter);
    void SingleBrDelete(MachineFunction *f);
};

#endif