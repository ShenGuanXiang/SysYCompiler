#ifndef __PUREFUNC_H__
#define __PUREFUNC_H__

#include "Unit.h"

// 对不写内存的函数调用，分析读的内存是否发生改变，消除多余的调用

class PureFunc
{
private:
    Unit *unit;

public:
    PureFunc(Unit *unit) : unit(unit){};
    void pass();
};

#endif