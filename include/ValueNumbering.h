#ifndef __VALUENUMBERING_H__
#define __VALUENUMBERING_H__

#include "Unit.h"
#include <unordered_map>
/*
    Local Value Numbering, I'll implement dominator-based algorithm later.
*/

class ValueNumbering
{
    Unit *unit;
    std::unordered_map<std::string,int> valueTable;
    std::unordered_map<int,Operand*> value2operand;
    unsigned valueNumber;
    std::string getOpString(Instruction *inst);
    unsigned getValueNumber() {return valueNumber++;}
public:
    ValueNumbering(Unit *unit) : unit(unit){valueNumber=0;};
    void dumpTable();
    void pass();
};

#endif