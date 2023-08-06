#ifndef __COMSUBEXPRELIM_H__
#define __COMSUBEXPRELIM_H__
#include "MachineCode.h"
#include "Unit.h"
#include <unordered_map>
#include <set>
/*
    Local Value Numbering, I'll implement dominator-based algorithm later.
*/
// 公共子表达式消除（DVNT实现）

class ComSubExprElim
{
    Unit *unit;
    std::unordered_map<std::string, Operand *> htable;
    // use def as value number, the hash is f: string->operand
    // construct operation's key from operand's name
    std::string getOpString(Instruction *inst);
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> domtree;

public:
    ComSubExprElim(Unit *unit) : unit(unit){};
    ComSubExprElim() {}
    void dumpTable();
    void computeDomTree(Function *func);
    void dvnt(BasicBlock *bb);
    void pass3();
    void pass2(Function *func) {}
    void pass1(BasicBlock* bb);
    // pass1 is implemented using LVN, but negelect the block boundaries, used in gcm
    // pass2 is implemented using SVN, i.e. optimize within extended blocks (tree shape), to be implemented
    // pass3 is implemented using DVNT, dominator-based
    // we only use dvnt, so pass1 and pass2 are removed
};
struct rplInfo
{
    MachineInstruction *inst;                                        // instrucion that defines the replaced operand
    std::vector<std::pair<MachineOperand **, MachineOperand *>> rpl; // places where the operand is used and the old machine operand
};
class ComSubExprElimASM
{
    MachineUnit *munit;
    std::unordered_map<std::string, MachineOperand *> htable;
    std::string getOpString(MachineInstruction *inst, bool lvn = false);
    std::unordered_map<MachineBlock *, std::vector<MachineBlock *>> domtree;
    std::set<MachineOperand> defset;
    std::set<MachineOperand> redef;

    std::set<MachineInstruction *> freeInsts;

public:
    ComSubExprElimASM(MachineUnit *munit) : munit(munit){};
    void findredef(MachineBlock *bb);
    void dumpTable();
    void computeDomTree(MachineFunction *func);
    void dvnt(MachineBlock *bb);
    void lvn(MachineBlock *bb);
    void pass();
};

#endif