#ifndef __REGISTER_ALLOCATION_H__
#define __REGISTER_ALLOCATION_H__
#include <set>
#include <map>
#include <vector>
#include <list>
#include <stack>
#include "MachineCode.h"

class MachineUnit;
class MachineOperand;
class MachineFunction;

struct Web
{
    std::set<MachineOperand *> defs;
    std::set<MachineOperand *> uses;
    bool spill;
    double spillCost;
    int sreg;
    int disp;
    int rreg;

    void Print()
    {
        if (defs != std::set<MachineOperand *>())
        {
            if (spillCost >= 1e8)
                fprintf(stderr, "%s:\tspill=%d;\tspillCost=INF\tsreg=%d\tdisp=%d\trreg=%d\n", (*defs.begin())->toStr().c_str(), spill, sreg, disp, rreg);
            else
                fprintf(stderr, "%s:\tspill=%d;\tspillCost=%lf\tsreg=%d\tdisp=%d\trreg=%d\n", (*defs.begin())->toStr().c_str(), spill, spillCost, sreg, disp, rreg);
        }
    }
};

class RegisterAllocation
{
public:
    struct DU
    {
        std::set<MachineOperand *> defs;
        std::set<MachineOperand *> uses;
        bool operator<(const DU &another) const
        {
            return (defs < another.defs || (defs == another.defs && uses < another.uses));
        }
    };

    MachineFunction *func;
    std::map<MachineOperand, std::set<DU>> du_chains;
    bool is_float;

    void makeDuChains();

private:
    MachineUnit *unit;
    int nregs;
    std::vector<int> pruneStack;
    std::vector<Web *> webs;
    std::map<MachineOperand *, int> operand2web;
    std::vector<std::vector<bool>> adjMtx;
    std::vector<std::set<int>> adjList;
    std::vector<std::set<int>> rmvList;
    int minColor(int);
    void makeWebs();
    void buildAdjMatrix();
    void buildAdjLists();
    void computeSpillCosts();
    void adjustIG(int i);
    void pruneGraph();
    bool regCoalesce();
    bool assignRegs();
    void modifyCode();
    void genSpillCode();
    bool isRightType(MachineOperand *);
    int Reg2WebIdx(int);
    int WebIdx2Reg(int);
    bool isInterestingReg(MachineOperand *);
    bool isImmWeb(Web *);

public:
    RegisterAllocation(MachineUnit *);
    void pass();
};

#endif