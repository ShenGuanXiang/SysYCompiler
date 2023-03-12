#ifndef __MULDIVMOD2BIT_H__
#define __MULDIVMOD2BIT_H__

#include "MachineCode.h"

class MulDivMod2Bit {
    MachineUnit* unit;

   public:
    MulDivMod2Bit(MachineUnit* unit) : unit(unit){};
    void pass();
    void mul2lsl();

};

#endif