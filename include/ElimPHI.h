#ifndef __ELIMPHI_H__
#define __ELIMPHI_H__

#include "Unit.h"

/*
    SSA Destruction:
        1) add critical edges
        2) convert pcopy to seq
*/

class ElimPHI
{
    Unit *unit;

public:
    ElimPHI(Unit *unit) : unit(unit){};
    void pass();
};

#endif