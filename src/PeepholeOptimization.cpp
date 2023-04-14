#include "PeepholeOptimization.h"


void PeepholeOptimization::pass() {
    // 注：等讨论的时候改dfs形式
    op1();

}


void PeepholeOptimization::op1() {
    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++) 
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++) 
        {
            auto block = *block_iter;
            if (block->getInsts().empty())  continue;

            auto curr_inst_iter = block->begin();
            auto next_inst_iter = next(curr_inst_iter, 1);

            std::set<MachineInstruction*> instToRemove;

            for (; next_inst_iter != block->end(); curr_inst_iter++, next_inst_iter++) 
            {
                auto curr_inst = *curr_inst_iter;
                auto next_inst = *next_inst_iter;
                if (curr_inst->isStore() && next_inst->isLoad()) {
                    // convert store and load into store and move
                    //     str v355, [v11]
                    //     ldr v227, [v11]
                    //     -----
                    //     str v355, [v11]
                    //     mov v227, v355

                    if (*curr_inst->getUse()[1] == *next_inst->getUse()[0]) 
                    {
                        if (curr_inst->getUse().size() <= 2 && next_inst->getUse().size() <= 1) 
                        {
                            auto dst = new MachineOperand(*next_inst->getDef()[0]);
                            auto src = new MachineOperand(*curr_inst->getUse()[0]);
                            MachineInstruction* new_inst = new MovMInstruction(block, dst->getValType()->isFloat() && src->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);
                            *next_inst_iter = new_inst;
                        } 
                        else if (curr_inst->getUse().size() > 2 && next_inst->getUse().size() > 1 && *curr_inst->getUse()[2] == *next_inst->getUse()[1])
                        {
                            auto dst = new MachineOperand(*next_inst->getDef()[0]);
                            auto src = new MachineOperand(*curr_inst->getUse()[0]);
                            MachineInstruction* new_inst = new MovMInstruction(block, dst->getValType()->isFloat() && src->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);
                            *next_inst_iter = new_inst;
                        }
                    }
                }
            }
            // for (auto inst : instToRemove) {
            //     block->remove(inst);
            // }
        }
    }
}

