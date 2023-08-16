#ifndef __MULTITHREAD_H__
#define __MULTITHREAD_H__
#include "LoopSimplify.h"

struct LoopInfo
{
    Induction indvar;
    int cmpType;                                  // eg CmpInstruction::GE
    std::pair<Operand *, Operand *> indvar_range; //[first,second)
    std::vector<BasicBlock *> loop_blocks;
    BasicBlock *loop_header;
    BasicBlock *loop_exiting_block;
};
class Multithread
{
    int nr_threads = 4;
    LoopInfo loop;
    void insert_opt_jump();

public:
    Multithread(); // auto find simple loop
    Multithread(LoopInfo loop_info) : loop(loop_info) {}
    void transform();
    void checkForm();
};
#endif // __MULTITHREAD_H__