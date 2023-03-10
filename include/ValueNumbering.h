#ifndef __VALUENUMBERING_H__
#define __VALUENUMBERING_H__

#include "Unit.h"
#include <unordered_map>
/*
    Local Value Numbering, I'll implement dominator-based algorithm later.
*/

typedef struct {
    std::unordered_map<std::string,int> valueTable;
    std::unordered_map<int,Operand*> value2operand;
}nrContext;

class ValueNumbering
{
    Unit *unit;
    std::unordered_map<std::string,int> valueTable;
    std::unordered_map<int,Operand*> value2operand;
    unsigned valueNumber;
    std::string getOpString(Instruction *inst);
    unsigned getValueNumber() {return valueNumber++;}

    std::vector<BasicBlock *> worklist;

public:
    ValueNumbering(Unit *unit) : unit(unit){valueNumber=0;};
    void dumpTable();
    void lvn(BasicBlock* bb,nrContext ctx); // a wrapper of `pass1`, used in pass2
    void svn(BasicBlock* bb,nrContext ctx);
    void pass2(Function* func);
    void pass1();
    //pass1 is implemented using LVN, i.e. optimize within basic blocks
    //pass2 is implemented using SVN, i.e. optimize within extended blocks (tree shape), to be implemented
};

#endif