#ifndef __VALUENUMBERING_H__
#define __VALUENUMBERING_H__

#include "Unit.h"
#include <unordered_map>
/*
    Local Value Numbering, I'll implement dominator-based algorithm later.
*/
// 公共子表达式消除（LVN实现）

class ValueNumbering
{
    Unit *unit;
    std::unordered_map<std::string,int> valueTable;
    std::unordered_map<std::string,Operand*> htable;
    // use def as value number, the hash is f: string->operand
    // construct operation's key from operand's name
    std::unordered_map<int,Operand*> value2operand;
    unsigned valueNumber;
    std::string getOpString(Instruction *inst);
    unsigned getValueNumber() {return valueNumber++;}
    std::unordered_map<BasicBlock*,std::vector<BasicBlock*>>domtree;
    std::vector<BasicBlock *> worklist;

public:
    ValueNumbering(Unit *unit) : unit(unit){valueNumber=0;};
    void dumpTable();
    void lvn(BasicBlock* bb); // a wrapper of `pass1`, used in pass2
    void svn(BasicBlock* bb){}
    void computeDomTree(Function* func);
    void dvnt(BasicBlock* bb);
    void pass3();
    void pass2(Function* func){}
    void pass1();
    //pass1 is implemented using LVN, i.e. optimize within basic blocks
    //pass2 is implemented using SVN, i.e. optimize within extended blocks (tree shape), to be implemented
    //pass3 is implemented using DVNT, dominator-based
};

#endif