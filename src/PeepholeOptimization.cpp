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
                // convert store and load into store and move
                if (curr_inst->isStore() && next_inst->isLoad()) 
                {
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

                // convert same loads to load and move
                else if (curr_inst->isLoad() && next_inst->isLoad()) 
                {
                    // 	   ldr r0, [fp, #-12]
                    //     ldr r1, [fp, #-12]
                    //     -----
                    // 	   ldr r0, [fp, #-12]
                    //     mov r1, r0
                    // in performance/fft

                    if (*curr_inst->getUse()[0] == *next_inst->getUse()[0] && 
                    ( (curr_inst->getUse().size() <= 1 && next_inst->getUse().size() <= 1)  || (curr_inst->getUse().size() > 1 && next_inst->getUse().size() > 1 && *curr_inst->getUse()[1] == *next_inst->getUse()[1]))) 
                    {
                        auto src = new MachineOperand(*curr_inst->getDef()[0]);
                        auto dst = new MachineOperand(*next_inst->getDef()[0]);
                        MachineInstruction* new_inst = new MovMInstruction(block, dst->getValType()->isFloat() && src->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);
                        *next_inst_iter = new_inst;
                    }
                }

                // array optimization after constant eval in asm
                // 这个test里好像没用到
                else if (curr_inst->isMov() && next_inst->isAdd()) 
                {
                    // 	   mov v1, #-120
                    //     add v0, fp, v1
                    //     -----
                    //     mov v1, #-120 (might be eliminated as dead code)
                    //     add v0, fp, #-120
                    if (*curr_inst->getDef()[0] == *next_inst->getUse()[1] && curr_inst->getUse()[0]->isImm()) {
                        auto dst = new MachineOperand(*next_inst->getDef()[0]);
                        auto src1 = new MachineOperand(*next_inst->getUse()[0]);
                        auto src2 = new MachineOperand(*curr_inst->getUse()[0]);
                        auto new_inst = new BinaryMInstruction( block, BinaryMInstruction::ADD, dst, src1, src2);
                        *next_inst_iter = new_inst;
                    }
                }

                // // fuse mul and add/sub
                // if (curr_inst->isMul() && next_inst->isAdd()) 
                // {
                // }
                
                // else if (curr_inst->isMul() && next_inst->isSub()) 
                // {
                // } 
                
                // else if (curr_inst->isVMul() && next_inst->isVAdd()) 
                // {
                // } 
                
                // else if (curr_inst->isVMul() && next_inst->isVSub()) 
                // {
                // } 

                // else if (curr_inst->isLoad() && next_inst->isVMov()) 
                // { 
                // }

            }
            for (auto inst : instToRemove) {
                block->removeInst(inst);
            }
        }
    }
}

