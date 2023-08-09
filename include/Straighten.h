#ifndef __STRAIGHTEN_H__
#define __STRAIGHTEN_H__

#include "Unit.h"

class Straighten {
private:
    MachineUnit *unit;

public:
    Straighten(MachineUnit *unit) : unit(unit){};
    void pass();
    void pass(MachineFunction* );
};

#endif