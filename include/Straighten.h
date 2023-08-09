#ifndef __STRAIGHTEN_H__
#define __STRAIGHTEN_H__

#include "MachineCode.h"
#include <set>

class Straighten {
private:
    MachineUnit *unit;
    std::map<int, std::pair<MachineBlock *, MachineBlock *>> blk2blk;
    // junctions存可以被合并到父节点的节点
    std::set<MachineBlock*> junctions;
    void getSlimBlock();
    void removeSlimBlock();

    void getJunctions();
    void mergeJunctions();
public:
    Straighten(MachineUnit *unit) : unit(unit){};
    void pass();
    void pass2();
};

#endif