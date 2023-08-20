#ifndef __MULTITHREAD_H__
#define __MULTITHREAD_H__
#include "LoopSimplify.h"
#include "LoopAnalyzer.h"

struct LoopInfo
{
    Instruction *cmp, *phi;
    std::pair<Operand *, Operand *> indvar_range; // [first,second)
    std::set<BasicBlock *> loop_blocks;
    BasicBlock *loop_header;
    BasicBlock *loop_exiting_block;
};
class Multithread
{
    Unit *unit;
    int nr_threads = 4;
    LoopInfo loop;
    void insert_opt_jump(BasicBlock *new_header);
    void compute_domtree();

public:
    Multithread(); // auto find simple loop
    Multithread(LoopInfo loop_info) : loop(loop_info) {}
    void transform();
    void checkForm();
};

std::vector<LoopInfo> findLoopInfo(Function *);

#endif // __MULTITHREAD_H__