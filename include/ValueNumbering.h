#ifndef __VALUENUMBERING_H__
#define __VALUENUMBERING_H__ 
#include "MachineCode.h"
#include "Unit.h"
#include <unordered_map>
#include <set>
/*
    Local Value Numbering, I'll implement dominator-based algorithm later.
*/
// 公共子表达式消除（DVNT实现）

class ValueNumbering
{
    Unit *unit;
    std::unordered_map<std::string,Operand*> htable;
    std::unordered_map<std::string,Operand*> global_htable;
    // use def as value number, the hash is f: string->operand
    // construct operation's key from operand's name
    std::unordered_map<BasicBlock*,std::vector<BasicBlock*>>domtree;


    //for dataflow analysis
    void addtoghtable(Operand* def,std::string instStr){
        if(global_htable.count(instStr)) {
            global_htable[def->toStr()]=global_htable[instStr];
        }
        else{
            global_htable[instStr]=def;
        }
    }
    std::string getOpString (Instruction *inst);
public:
    //export for dataflow analysis
    std::unordered_map<std::string,Operand*>& getmap() {return global_htable;}
    


    ValueNumbering(Unit *unit) : unit(unit){};
    void dumpTable();
    void computeDomTree(Function* func);
    void dvnt(BasicBlock* bb);
    void pass3();
    void pass2(Function* func){}
    void pass1(){};
    //pass1 is implemented using LVN, i.e. optimize within basic blocks
    //pass2 is implemented using SVN, i.e. optimize within extended blocks (tree shape), to be implemented
    //pass3 is implemented using DVNT, dominator-based
    //we only use dvnt, so pass1 and pass2 are removed
};

class ValueNumberingASM
{
    MachineUnit* munit;
    std::unordered_map<std::string,MachineOperand*> htable;
    std::string getOpString(MachineInstruction *inst);
    std::unordered_map<MachineBlock*,std::vector<MachineBlock*>>domtree;
    std::set<MachineOperand> defset;
    std::set<MachineOperand> redef;
public:
    ValueNumberingASM(MachineUnit* munit) : munit(munit){};
    void findredef(MachineBlock* bb);
    void dumpTable();
    void computeDomTree(MachineFunction* func);
    void dvnt(MachineBlock* bb);
    void pass();
};

#endif