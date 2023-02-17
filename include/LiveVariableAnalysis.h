#ifndef __LIVE_VARIABLE_ANALYSIS_H__
#define __LIVE_VARIABLE_ANALYSIS_H__

#include <set>
#include <map>

// https://decaf-lang.github.io/minidecaf-tutorial/docs/step7/dataflow.html
class Operand;
class BasicBlock;
class Function;
class Unit;
class LiveVariableAnalysis
{
private:
    std::map<Operand, std::set<Operand *>> all_uses;
    std::map<BasicBlock *, std::set<Operand *>> def, use;
    void computeUsePos(Function *);
    void computeDefUse(Function *);
    void computeLiveInOut(Function *);

public:
    void pass(Unit *unit);
    void pass(Function *func);
    std::map<Operand, std::set<Operand *>> &getAllUses() { return all_uses; };
};

class MachineOperand;
class MachineBlock;
class MachineFunction;
class MachineUnit;
class MLiveVariableAnalysis
{
private:
    std::map<MachineOperand, std::set<MachineOperand *>> all_uses;
    std::map<MachineBlock *, std::set<MachineOperand *>> def, use;
    void computeUsePos(MachineFunction *);
    void computeDefUse(MachineFunction *);
    void computeLiveInOut(MachineFunction *);

public:
    void pass(MachineUnit *unit);
    void pass(MachineFunction *func);
    std::map<MachineOperand, std::set<MachineOperand *>> &getAllUses() { return all_uses; };
};

#endif