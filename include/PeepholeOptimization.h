#ifndef __PEEPHOLE_OPTIMIZATION_H__
#define __PEEPHOLE_OPTIMIZATION_H__

#include "MachineCode.h"

class PeepholeOptimization {

    MachineUnit *unit;

public:
    PeepholeOptimization(MachineUnit* unit): unit(unit) {};
    void pass();
    void op1();
    void op2();
    void op3();
    // void op4();
};

#endif 