#ifndef __LIVE_VARIABLE_ANALYSIS_H__
#define __LIVE_VARIABLE_ANALYSIS_H__

#include <set>
#include <map>

// https://decaf-lang.github.io/minidecaf-tutorial/docs/step7/dataflow.html
class Operand;
class BasicBlock;
class Function;
class Unit;
// class LiveVariableAnalysis
// {
// private:
//     std::map<BasicBlock *, std::set<Operand *>> def, use;
//     void computeDefUse(Function *);
//     void computeLiveInOut(Function *);

// public:
//     void pass(Unit *unit);
//     void pass(Function *func);
// };

class MachineOperand;
class MachineBlock;
class MachineFunction;
class MachineUnit;
class MLiveVariableAnalysis
{
private:
    MachineUnit *unit;
    std::map<MachineOperand, std::set<MachineOperand*>> all_uses;
public:
    MLiveVariableAnalysis(MachineUnit *unit) : unit(unit){};
    std::map<MachineOperand, std::set<MachineOperand*>>& getAllUses() { return all_uses; };
    void pass();
    void pass(MachineFunction* f);
};

#endif