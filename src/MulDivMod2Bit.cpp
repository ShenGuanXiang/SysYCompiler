#include "MulDivMod2Bit.h"
#include <cmath>

using namespace std;

static std::set<MachineInstruction *> freeInsts;

inline bool is2Exp(int val)
{
    return !(val & (val - 1));
}

void MulDivMod2Bit::pass()
{
    mul2lsl(); // 乘法 to 移位
}

void MulDivMod2Bit::mul2lsl()
{
    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
        {
            auto block = *block_iter;
            if (block->getInsts().empty())
            {
                continue;
            }
            auto curr_inst_iter = block->begin();
            auto next_inst_iter = next(curr_inst_iter, 1);
            for (; next_inst_iter != block->end(); curr_inst_iter++, next_inst_iter++)
            {
                auto curr_inst = *curr_inst_iter;
                auto next_inst = *next_inst_iter;

                // example:
                // ldr r5, #2
                // mul r6, r4, r5
                // -------->
                // mov r6, r4, LSL#1

                //    if (curr_inst->isLoad() && ((LoadMInstruction*)curr_inst)->is_1_src() && next_inst->isMul())
                if ((curr_inst->isMov() || (curr_inst->isLoad() && ((LoadMInstruction *)curr_inst)->is_1_src())) && next_inst->isMul())
                {
                    auto Dst = curr_inst->getDef()[0]; // r5
                    auto Src = curr_inst->getUse()[0]; // #2

                    auto mulDst = next_inst->getDef()[0];  // r6
                    auto mulSrc1 = next_inst->getUse()[0]; // r4
                    auto mulSrc2 = next_inst->getUse()[1]; // r5

                    // 判断 mov/load指令的目的操作数 是否属于 两个乘法操作数之，例如example里的 ldr r5, #2 和 mul r6, r4, r5 都有 r5
                    if ((*mulSrc1 == *Dst || *mulSrc2 == *Dst) && Src->isImm() && !Src->getValType()->isFloat() && Src->getVal() != 0 && is2Exp(Src->getVal()))
                    {
                        assert(curr_inst->getCond() == MachineInstruction::NONE);
                        // 让移位操作数等于另一个（不在mov/load里的）乘法操作数
                        MachineOperand *lslSrc = (*mulSrc1 == *Dst) ? mulSrc2 : mulSrc1;                                                                                  // r4
                        auto mov_lsl = new MovMInstruction(block, MovMInstruction::MOVLSL, mulDst, lslSrc, new MachineOperand(MachineOperand::IMM, log2(Src->getVal()))); // mov r6, r4, LSL#1

                        freeInsts.insert(next_inst);
                        *(next_inst_iter) = mov_lsl;
                    }
                }

                // example:
                // mov r6, #2
                // mov r8, #4
                // mul r9, r6, r8
                // -------->
                // mov r8, #4
                // mov r9, r8, LSL#1

                if (next_inst_iter + 1 != block->end())
                {
                    auto next_next_inst = *(next_inst_iter + 1);
                    if ((curr_inst->isMov() || (curr_inst->isLoad() && ((LoadMInstruction *)curr_inst)->is_1_src())) && next_next_inst->isMul())
                    {
                        auto Dst = curr_inst->getDef()[0]; // r6
                        auto Src = curr_inst->getUse()[0]; // #2

                        auto mulDst = next_next_inst->getDef()[0];  // r9
                        auto mulSrc1 = next_next_inst->getUse()[0]; // r6
                        auto mulSrc2 = next_next_inst->getUse()[1]; // r8

                        // 判断 mov/load指令的目的操作数 是否属于 两个乘法操作数之，例如example里的 mov r6, #2 和 mul r9, r6, r8 都有 r6
                        if ((*mulSrc1 == *Dst || *mulSrc2 == *Dst) && Src->isImm() && !Src->getValType()->isFloat() && Src->getVal() != 0 && is2Exp(Src->getVal()))
                        {
                            assert(curr_inst->getCond() == MachineInstruction::NONE);
                            // 让移位操作数等于另一个（不在mov/load里的）乘法操作数
                            MachineOperand *lslSrc = (*mulSrc1 == *Dst) ? mulSrc2 : mulSrc1;                                                                                  // = mulSrc2 = r8
                            auto mov_lsl = new MovMInstruction(block, MovMInstruction::MOVLSL, mulDst, lslSrc, new MachineOperand(MachineOperand::IMM, log2(Src->getVal()))); // mov r9, r8, LSL#1

                            freeInsts.insert(next_next_inst);
                            *(next_inst_iter + 1) = mov_lsl;
                        }
                    }
                }
            }
        }
    }

    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
}