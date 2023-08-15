#ifndef __SPARSECONDCONSTPROP_H__
#define __SPARSECONDCONSTPROP_H__

#include "Unit.h"

class SparseCondConstProp
{
private:
    struct EQ_COND_DATA
    {
        Instruction *inst;
        Operand *left_op;
        Operand *right_op;
        BasicBlock *pre_bb;
    };

    Unit *unit;
    std::map<Operand *, int> status_map;
    std::map<Operand *, float> value_map;
    std::set<std::pair<BasicBlock *, BasicBlock *>> marked;
    std::vector<std::pair<BasicBlock *, BasicBlock *>> cfg_worklist;
    std::vector<Instruction *> ssa_worklist;
    std::vector<EQ_COND_DATA> eq_cond_worklist;

public:
    enum
    {
        UNDEF,
        CONST,
        NAC
    } STATE;
    SparseCondConstProp(Unit *unit) : unit(unit){};
    void pass();
    void pass(Function *);
    void visit(Instruction *);
    void constFold(Function *);
    void printStat();
};

#endif