#ifndef __ALGSIMPLIFY_H__
#define __ALGSIMPLIFY_H__

#include "Unit.h"

// 代数化简

class AlgSimplify
{
private:
    Unit *unit;

public:
    AlgSimplify(Unit *unit) : unit(unit){};
    void pass();
    void pass(Function *);
};

#endif