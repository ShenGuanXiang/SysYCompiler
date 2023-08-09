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
private:
    MachineUnit *unit;
    int nregs;
    double defWt;
    double useWt;
    double copyWt;
    std::vector<int> pruneStack;
    MachineFunction *func;
    // std::map<MachineOperand *, std::set<MachineOperand *>> du_chains;
    std::map<MachineOperand, std::set<DU>> du_chains;
    std::vector<Web *> webs;
    std::map<MachineOperand *, int> operand2web;
    std::vector<std::vector<bool>> adjMtx;
    std::vector<std::vector<int>> adjList;
    std::vector<std::vector<int>> rmvList;
    int minColor(int);
    void makeDuChains();
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

public:
    RegisterAllocation(MachineUnit *);
    void pass();
};

#endif