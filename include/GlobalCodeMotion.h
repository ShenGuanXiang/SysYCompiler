#ifndef _GLOBAL_CODE_MOTION_H_
#define _GLOBAL_CODE_MOTION_H_

#include <stack>
#include <set>
#include <string>
#include <unordered_map>
#include "Unit.h"
#include "ComSubExprElim.h"


//helper
class Helper{
    std::unordered_map<BasicBlock*,int> dom_depth;
    std::unordered_map<BasicBlock*,int> loop_depth;
    std::set<BasicBlock*>bb_visited;
public:
    std::unordered_map<Instruction*,bool> visited;
    std::unordered_map<BasicBlock*,Instruction*> append_points;
    std::unordered_map<BasicBlock*,Instruction*> prepend_points;

    void post_order(BasicBlock* bb,std::stack<BasicBlock*> &s);
    void compute_info(Function *func);
    void clear_visited(Function *func);
    int get_dom_depth(BasicBlock* bb) {return dom_depth[bb];}
    int get_loop_depth(BasicBlock* bb) {return loop_depth[bb];}
    BasicBlock* find_lca(BasicBlock* a,BasicBlock* b);
    bool is_pinned(Instruction* inst){
        return inst->isPHI() || inst->isRet() || inst->isCond() || inst->isUncond() || inst->isStore() || inst->isLoad() 
        || inst->isCall() || inst->isCmp();
    }
    // TODO：抛出异常的指令不能向前移动
};

class GlobalCodeMotion{
    Unit* unit;
    Helper h;

    std::unordered_map<Instruction*,BasicBlock*> schedule_block;
    std::unordered_map<std::string,Operand*> htable;

    void gvn(Function* func);

    void schedule_early(Instruction* inst);
    void schedule_late(Instruction* inst);
    void move(Instruction* inst);
    void pass(Function* func);
    
    void print_schedule();
public:
    unsigned move_count = 0;
    unsigned load_count = 0;
    GlobalCodeMotion(Unit* unit):unit(unit){};   
    void pass();
};

#endif // _GLOBAL_CODE_MOTION_H_