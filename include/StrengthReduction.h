#ifndef __STRENGTHREDUCTION_H__
#define __STRENGTHREDUCTION_H__

#include "MachineCode.h"
#include <map>
#include <vector>

class StrengthReduction
{
    MachineUnit *unit;
    std::map<MachineBlock *, std::vector<MachineBlock *>> domtree;

public:
    StrengthReduction(MachineUnit *unit) : unit(unit) { domtree = std::map<MachineBlock *, std::vector<MachineBlock *>>(); };
    void pass();
    void dfs(MachineBlock *bb, std::map<MachineOperand, int> op2val);
    void dfs(MachineBlock *bb, std::map<MachineOperand, float> op2val);
    // void mul2lsl();
    // void div2mul();
};

#endif