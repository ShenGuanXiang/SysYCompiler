#ifndef __STRAIGHTEN_H__
#define __STRAIGHTEN_H__

#include "MachineCode.h"
#include <set>

class Straighten
{
private:
    MachineUnit *unit;

public:
    Straighten(MachineUnit *unit) : unit(unit){};
    void pass();
};

#endif