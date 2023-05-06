/**
 * linear scan register allocation
 */

#ifndef _LINEARSCAN_H__
#define _LINEARSCAN_H__
#include <set>
#include <map>
#include <vector>
#include <list>
#include "Type.h"

class MachineUnit;
class MachineOperand;
class MachineFunction;

class LinearScan
{
public:
    struct Interval
    {
        int start;
        int end;
        int disp;     // displacement in stack
        int real_reg; // the real register mapped from virtual register if the vreg is not spilled to memory, else -1
        Type *valType;
        std::set<MachineOperand *> defs;
        std::set<MachineOperand *> uses;
    };
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
    MachineFunction *func;
    std::vector<int> rregs;
    std::vector<int> sregs; // 浮点可分配寄存器号
    std::map<MachineOperand, std::set<DU>> du_chains;
    std::vector<Interval *> intervals, active, regIntervals;
    void releaseAllRegs();
    bool isInterestingReg(MachineOperand *);
    void expireOldIntervals(Interval *interval);
    void insertActiveRegIntervals(Interval *interval);
    void spillAtInterval(Interval *interval);
    void makeDuChains();
    void computeLiveIntervals();
    bool linearScanRegisterAllocation();
    void modifyCode();
    void genSpillCode();

public:
    LinearScan(MachineUnit *unit);
    void pass();
};

#endif