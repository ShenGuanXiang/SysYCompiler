#ifndef __MACHINEDEADCODEELIMINATION_H__
#define __MACHINEDEADCODEELIMINATION_H__

#include "Unit.h"

/*
    Dead Instr Elimination for MachineCode:
*/
class MachineDeadCodeElimination
{
private:
    MachineUnit* unit;

public:
    MachineDeadCodeElimination(MachineUnit *unit) : unit(unit){};
    void pass();
    void pass(MachineFunction* f);
    void SingleBrDelete(MachineFunction* f);
};

#endif