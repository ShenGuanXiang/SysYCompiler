#include "PeepholeOptimization.h"
#include "DeadCodeElim.h"

static std::vector<MachineInstruction *> freeInsts;

void PeepholeOptimization::pass()
{
    op1();
    MachineDeadCodeElim mdce1(unit);
    mdce1.pass(false); // 死代码消除
    op2();
    MachineDeadCodeElim mdce2(unit);
    mdce2.pass(false); // 死代码消除
    op3();
    MachineDeadCodeElim mdce3(unit);
    mdce3.pass(false); // 死代码消除
    op4();
    MachineDeadCodeElim mdce4(unit);
    mdce4.pass(false); // 死代码消除

    for (auto inst : freeInsts)
    {
        if (inst)
        {
            delete inst;
            inst = NULL;
        }
        else
            assert(0);
    }
    freeInsts.clear();
}

void PeepholeOptimization::op1()
{

    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
        {
            auto block = *block_iter;
            if (block->getInsts().empty())
                continue;

            std::vector<MachineInstruction *> insts;
            insts.assign(block->begin(), block->end());
            auto curr_inst_iter = insts.begin();
            auto next_inst_iter = next(curr_inst_iter, 1);

            for (; next_inst_iter != insts.end(); curr_inst_iter++, next_inst_iter++)
            {
                auto curr_inst = *curr_inst_iter;
                auto next_inst = *next_inst_iter;

                if (curr_inst->isAdd() && next_inst->isLoad())
                {
                    // add r0, fp, #-12
                    // ldr r1, [r0]
                    // --->
                    // add r0, fp, #-12
                    // ldr r1, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 1 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[0] && curr_inst->getUse()[1]->isImm())
                    {
                        if ((next_inst->getDef()[0]->getValType()->isFloat() && (curr_inst->getUse()[1]->isIllegalShifterOperand() || curr_inst->getUse()[1]->getVal() < -1023 || curr_inst->getUse()[1]->getVal() > 1023)) || (next_inst->getDef()[0]->getValType()->isInt() && (curr_inst->getUse()[1]->getVal() < -4095 || curr_inst->getUse()[1]->getVal() > 4095)))
                            continue;
                        auto new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                    else if (next_inst->getUse().size() == 1 &&
                             *curr_inst->getDef()[0] == *next_inst->getUse()[0] && (curr_inst->getUse()[1]->isReg() || curr_inst->getUse()[1]->isVReg()))
                    {
                        if (next_inst->getDef()[0]->getValType()->isFloat())
                            continue;
                        auto new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isSub() && next_inst->isLoad())
                {
                    // sub r0, fp, #12
                    // ldr r1, [r0]
                    // --->
                    // sub r0, fp, #12
                    // ldr r1, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 1 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[0] && curr_inst->getUse()[1]->isImm())
                    {
                        auto src_imm = new MachineOperand(MachineOperand::IMM, -1 * curr_inst->getUse()[1]->getVal(), TypeSystem::constIntType);
                        if ((next_inst->getDef()[0]->getValType()->isFloat() && (src_imm->isIllegalShifterOperand() || src_imm->getVal() < -1023 || src_imm->getVal() > 1023)) || (next_inst->getDef()[0]->getValType()->isInt() && (src_imm->getVal() < -4095 || src_imm->getVal() > 4095)))
                            continue;
                        auto new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), src_imm);
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isAdd() && next_inst->isStore())
                {
                    // add r0, fp, #-12
                    // str r1, [r0]
                    // --->
                    // add r0, fp, #-12
                    // str r1, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[1] && curr_inst->getUse()[1]->isImm())
                    {
                        if ((next_inst->getUse()[0]->getValType()->isFloat() && (curr_inst->getUse()[1]->isIllegalShifterOperand() || curr_inst->getUse()[1]->getVal() < -1023 || curr_inst->getUse()[1]->getVal() > 1023)) || (next_inst->getUse()[0]->getValType()->isInt() && (curr_inst->getUse()[1]->getVal() < -4095 || curr_inst->getUse()[1]->getVal() > 4095)))
                            continue;
                        auto new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }

                    else if (next_inst->getUse().size() == 2 &&
                             *curr_inst->getDef()[0] == *next_inst->getUse()[1] && (curr_inst->getUse()[1]->isReg() || curr_inst->getUse()[1]->isVReg()))
                    {
                        if (next_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        auto new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isSub() && next_inst->isStore())
                {
                    // sub r0, fp, #12
                    // str r1, [r0]
                    // --->
                    // sub r0, fp, #12
                    // str r1, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[1] && curr_inst->getUse()[1]->isImm())
                    {
                        auto src_imm = new MachineOperand(MachineOperand::IMM, -1 * curr_inst->getUse()[1]->getVal(), TypeSystem::constIntType);
                        if ((next_inst->getUse()[0]->getValType()->isFloat() && (src_imm->isIllegalShifterOperand() || src_imm->getVal() < -1023 || src_imm->getVal() > 1023)) || (next_inst->getUse()[0]->getValType()->isInt() && (src_imm->getVal() < -4095 || src_imm->getVal() > 4095)))
                            continue;
                        auto new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), src_imm);
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                // convert store and load into store and move
                else if (curr_inst->isStore() && next_inst->isLoad())
                {
                    //     str v355, [v11]
                    //     ldr v227, [v11]
                    //     -----
                    //     str v355, [v11]
                    //     mov v227, v355

                    if (*curr_inst->getUse()[1] == *next_inst->getUse()[0])
                    {
                        if (curr_inst->getUse().size() == 2 && next_inst->getUse().size() == 1)
                        {
                            auto dst = new MachineOperand(*next_inst->getDef()[0]);
                            auto src = new MachineOperand(*curr_inst->getUse()[0]);
                            MachineInstruction *new_inst = new MovMInstruction(block, dst->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);

                            block->insertBefore(next_inst, new_inst);
                            block->removeInst(next_inst);
                            freeInsts.push_back(next_inst);
                            // *next_inst_iter = new_inst;
                        }
                        else if (curr_inst->getUse().size() > 2 && next_inst->getUse().size() > 1 && *curr_inst->getUse()[2] == *next_inst->getUse()[1])
                        {
                            auto dst = new MachineOperand(*next_inst->getDef()[0]);
                            auto src = new MachineOperand(*curr_inst->getUse()[0]);
                            MachineInstruction *new_inst = new MovMInstruction(block, dst->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);

                            block->insertBefore(next_inst, new_inst);
                            block->removeInst(next_inst);
                            freeInsts.push_back(next_inst);
                            // *next_inst_iter = new_inst;
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
                        ((curr_inst->getUse().size() == 1 && next_inst->getUse().size() == 1) || (curr_inst->getUse().size() == 2 && next_inst->getUse().size() == 2 && *curr_inst->getUse()[1] == *next_inst->getUse()[1])))
                    {
                        if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || (curr_inst->getUse().size() == 2 && *curr_inst->getDef()[0] == *curr_inst->getUse()[1]))
                            continue;
                        auto src = new MachineOperand(*curr_inst->getDef()[0]);
                        auto dst = new MachineOperand(*next_inst->getDef()[0]);
                        MachineInstruction *new_inst = new MovMInstruction(block, dst->getValType()->isFloat() ? MovMInstruction::VMOV : MovMInstruction::MOV, dst, src);

                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                        // *next_inst_iter = new_inst;
                    }
                }

                // // array optimization after constant eval in asm
                // // 这个test里好像没用到
                // else if (curr_inst->isMov() && next_inst->isAdd())
                // {
                //     // 	   mov v1, #-120
                //     //     add v0, fp, v1
                //     //     -----
                //     //     mov v1, #-120 (might be eliminated as dead code)
                //     //     add v0, fp, #-120
                //     if (*curr_inst->getDef()[0] == *next_inst->getUse()[1] && curr_inst->getUse()[0]->isImm())
                //     {
                //         auto dst = new MachineOperand(*next_inst->getDef()[0]);
                //         auto src1 = new MachineOperand(*next_inst->getUse()[0]);
                //         auto src2 = new MachineOperand(*curr_inst->getUse()[0]);
                //         auto new_inst = new BinaryMInstruction(block, BinaryMInstruction::ADD, dst, src1, src2);
                //         block->insertBefore(next_inst, new_inst);
                //         block->removeInst(next_inst);
                //         freeInsts.push_back(next_inst);
                //         // *next_inst_iter = new_inst;
                //     }
                // }

                // fuse mul and add
                else if (curr_inst->isMul() && next_inst->isAdd())
                {

                    //     mul v0, v1, v2
                    //     add v3, v4, v0
                    //     -----
                    //     mla v3, v1, v2, v4
                    //
                    //     mul v0, v1, v2
                    //     sub v3, v4, v0
                    //     -----
                    //     mls v3, v1, v2, v4

                    // mla/mls rd, rn, rm, ra
                    // https://developer.arm.com/documentation/dui0489/c/arm-and-thumb-instructions/multiply-instructions/mul--mla--and-mls
                    // FIXME: problem at functional/71_full_conn.

                    // fix：
                    //     mul v0, v1, v0
                    //     add v3, v4, v0
                    //     -----
                    //     mla v3, v1, v0, v4
                    //     mul v0, v1, v0

                    //     mul v0, v3, v0
                    //     add v3, v4, v0
                    //     -----
                    //     mov a, v0
                    //     mov b, v3
                    //     mul v0, v3, v0
                    //     mla v3, b, a, v4

                    auto mul_dst = curr_inst->getDef()[0];
                    auto add_src1 = next_inst->getUse()[0];
                    auto add_src2 = next_inst->getUse()[1];

                    if (*mul_dst == *add_src1 || *mul_dst == *add_src2)
                    {
                        auto rd = new MachineOperand(*(next_inst->getDef()[0]));
                        auto rn = new MachineOperand(*(curr_inst->getUse()[0]));
                        auto rm = new MachineOperand(*(curr_inst->getUse()[1]));
                        MachineOperand *ra;
                        if (*mul_dst == *add_src1)
                            ra = new MachineOperand(*(next_inst->getUse()[1]));
                        else
                            ra = new MachineOperand(*(next_inst->getUse()[0]));

                        if (ra->isImm())
                            continue;

                        if (!curr_inst->getDef()[0]->getValType()->isFloat() && !next_inst->getDef()[0]->getValType()->isFloat())
                        {
                            if (*mul_dst == *rd)
                            {
                                auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLA, rd, rn, rm, ra);

                                block->insertBefore(next_inst, fused_inst);
                                block->removeInst(next_inst);
                                freeInsts.push_back(next_inst);
                                block->removeInst(curr_inst);
                                freeInsts.push_back(curr_inst);
                            }
                            // 交换顺序
                            else if (*mul_dst == *rn || *mul_dst == *rm)
                            {
                                // 特例：添加mov(多余的mov会在寄存器分配删除)
                                if (*rd == *rn || *rd == *rm)
                                {
                                    auto new_rn = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto new_rm = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto new_ra = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto mov_rn = new MovMInstruction(block, MovMInstruction::MOV, new_rn, rn);
                                    auto mov_rm = new MovMInstruction(block, MovMInstruction::MOV, new_rm, rm);
                                    auto mov_ra = new MovMInstruction(block, MovMInstruction::MOV, new_ra, ra);

                                    new_rn = new MachineOperand(*new_rn);
                                    new_rm = new MachineOperand(*new_rm);
                                    new_ra = new MachineOperand(*new_ra);
                                    auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLA, rd, new_rn, new_rm, new_ra);

                                    // mov
                                    // mul
                                    // mla
                                    block->insertBefore(curr_inst, mov_rn);
                                    block->insertBefore(curr_inst, mov_rm);
                                    block->insertBefore(curr_inst, mov_ra);

                                    block->insertBefore(next_inst, fused_inst);
                                    block->removeInst(next_inst);
                                    freeInsts.push_back(next_inst);
                                    // *next_inst_iter = fused_inst;
                                }
                                else
                                {
                                    auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLA, rd, rn, rm, ra);

                                    block->insertBefore(curr_inst, fused_inst);
                                    block->removeInst(next_inst);
                                    freeInsts.push_back(next_inst);
                                }
                            }
                            else
                            {
                                // 直接保留mul
                                auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLA, rd, rn, rm, ra);

                                block->insertBefore(curr_inst, fused_inst);
                                block->removeInst(next_inst);
                                freeInsts.push_back(next_inst);
                                // *next_inst_iter = fused_inst;
                            }
                        }

                        // 效果不大，且vmla def-use关系混乱 TODO：在寄存器分配完毕后分情况讨论
                        // else if (curr_inst->getDef()[0]->getValType()->isFloat() && next_inst->getDef()[0]->getValType()->isFloat())
                        // {

                        //     // vmul a b c
                        //     // vadd d e a
                        //     // -----------------
                        //     // vmov nb b
                        //     // vmov nc c
                        //     // vmul a nb nc
                        //     // vmov ne e
                        //     // vmla e nb nc
                        //     // vmov d e
                        //     // vmov e ne

                        //     // 多余的vmov在dce&寄存器分配会被删除

                        //     // b
                        //     auto new_rn = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // c
                        //     auto new_rm = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // e
                        //     auto new_ra = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // vmov
                        //     auto mov_rn = new MovMInstruction(block, MovMInstruction::VMOV, new_rn, rn);
                        //     auto mov_rm = new MovMInstruction(block, MovMInstruction::VMOV, new_rm, rm);
                        //     // vmul
                        //     auto new_mul_dst = new MachineOperand(*(curr_inst->getDef()[0]));
                        //     new_rn = new MachineOperand(*new_rn);
                        //     new_rm = new MachineOperand(*new_rm);
                        //     auto new_mul = new BinaryMInstruction(block, BinaryMInstruction::MUL, new_mul_dst, new_rn, new_rm);
                        //     // vmov
                        //     auto mov_ra = new MovMInstruction(block, MovMInstruction::VMOV, new_ra, ra);
                        //     // vmla
                        //     new_rn = new MachineOperand(*new_rn);
                        //     new_rm = new MachineOperand(*new_rm);
                        //     auto fused_inst = new VMLASMInstruction(block, VMLASMInstruction::VMLA, new MachineOperand(*ra), new_rn, new_rm);
                        //     // vmov
                        //     auto mov_de = new MovMInstruction(block, MovMInstruction::VMOV, rd, new MachineOperand(*ra));
                        //     auto mov_ene = new MovMInstruction(block, MovMInstruction::VMOV, new MachineOperand(*ra), new MachineOperand(*new_ra));

                        //     block->insertBefore(curr_inst, mov_rn);
                        //     block->insertBefore(curr_inst, mov_rm);
                        //     block->insertBefore(curr_inst, new_mul);
                        //     block->insertBefore(curr_inst, mov_ra);
                        //     block->insertBefore(curr_inst, fused_inst);
                        //     block->insertBefore(curr_inst, mov_de);
                        //     block->insertBefore(curr_inst, mov_ene);

                        //     block->removeInst(curr_inst);
                        //     freeInsts.push_back(curr_inst);
                        //     block->removeInst(next_inst);
                        //     freeInsts.push_back(next_inst);
                        // }
                    }
                }

                // fuse mul and sub
                else if (curr_inst->isMul() && next_inst->isSub())
                {
                    auto mul_dst = curr_inst->getDef()[0];
                    // auto sub_src1 = next_inst->getUse()[0];
                    auto sub_src2 = next_inst->getUse()[1];

                    if (*mul_dst == *sub_src2)
                    {
                        auto rd = new MachineOperand(*(next_inst->getDef()[0]));
                        auto rn = new MachineOperand(*(curr_inst->getUse()[0]));
                        auto rm = new MachineOperand(*(curr_inst->getUse()[1]));
                        auto ra = new MachineOperand(*(next_inst->getUse()[0]));

                        if (!curr_inst->getDef()[0]->getValType()->isFloat() && !next_inst->getDef()[0]->getValType()->isFloat())
                        {
                            if (*mul_dst == *rd)
                            {
                                auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLS, rd, rn, rm, ra);

                                block->insertBefore(next_inst, fused_inst);
                                block->removeInst(next_inst);
                                freeInsts.push_back(next_inst);
                                block->removeInst(curr_inst);
                                freeInsts.push_back(curr_inst);
                            }
                            // 交换顺序
                            else if (*mul_dst == *rn || *mul_dst == *rm)
                            {
                                // 特例：添加mov(多余的mov会在寄存器分配删除)
                                if (*rd == *rn || *rd == *rm)
                                {
                                    auto new_rn = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto new_rm = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto new_ra = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                                    auto mov_rn = new MovMInstruction(block, MovMInstruction::MOV, new_rn, rn);
                                    auto mov_rm = new MovMInstruction(block, MovMInstruction::MOV, new_rm, rm);
                                    auto mov_ra = new MovMInstruction(block, MovMInstruction::MOV, new_ra, ra);

                                    new_rn = new MachineOperand(*new_rn);
                                    new_rm = new MachineOperand(*new_rm);
                                    new_ra = new MachineOperand(*new_ra);
                                    auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLS, rd, new_rn, new_rm, new_ra);

                                    // mov
                                    // mul
                                    // mla
                                    block->insertBefore(curr_inst, mov_rn);
                                    block->insertBefore(curr_inst, mov_rm);
                                    block->insertBefore(curr_inst, mov_ra);

                                    block->insertBefore(next_inst, fused_inst);
                                    block->removeInst(next_inst);
                                    freeInsts.push_back(next_inst);
                                }
                                else
                                {
                                    auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLS, rd, rn, rm, ra);

                                    block->insertBefore(curr_inst, fused_inst);
                                    block->removeInst(next_inst);
                                    freeInsts.push_back(next_inst);
                                }
                            }
                            else
                            {
                                // 直接保留mul
                                auto fused_inst = new MLASMInstruction(block, MLASMInstruction::MLS, rd, rn, rm, ra);

                                block->insertBefore(next_inst, fused_inst);
                                block->removeInst(next_inst);
                                freeInsts.push_back(next_inst);
                            }
                        }

                        // 效果不大，且vmla def-use关系混乱 TODO：在寄存器分配完毕后分情况讨论
                        // else if (curr_inst->getDef()[0]->getValType()->isFloat() && next_inst->getDef()[0]->getValType()->isFloat())
                        // {
                        //     // b
                        //     auto new_rn = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // c
                        //     auto new_rm = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // e
                        //     auto new_ra = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel(), TypeSystem::floatType);

                        //     // vmov
                        //     auto mov_rn = new MovMInstruction(block, MovMInstruction::VMOV, new_rn, rn);
                        //     auto mov_rm = new MovMInstruction(block, MovMInstruction::VMOV, new_rm, rm);
                        //     // vmul
                        //     auto new_mul_dst = new MachineOperand(*(curr_inst->getDef()[0]));
                        //     new_rn = new MachineOperand(*new_rn);
                        //     new_rm = new MachineOperand(*new_rm);
                        //     auto new_mul = new BinaryMInstruction(block, BinaryMInstruction::MUL, new_mul_dst, new_rn, new_rm);
                        //     // vmov
                        //     auto mov_ra = new MovMInstruction(block, MovMInstruction::VMOV, new_ra, ra);
                        //     // vmls
                        //     new_rn = new MachineOperand(*new_rn);
                        //     new_rm = new MachineOperand(*new_rm);
                        //     auto fused_inst = new VMLASMInstruction(block, VMLASMInstruction::VMLS, new MachineOperand(*ra), new_rn, new_rm);
                        //     // vmov
                        //     auto mov_de = new MovMInstruction(block, MovMInstruction::VMOV, rd, new MachineOperand(*ra));
                        //     auto mov_ene = new MovMInstruction(block, MovMInstruction::VMOV, new MachineOperand(*ra), new MachineOperand(*new_ra));

                        //     block->insertBefore(curr_inst, mov_rn);
                        //     block->insertBefore(curr_inst, mov_rm);
                        //     block->insertBefore(curr_inst, new_mul);
                        //     block->insertBefore(curr_inst, mov_ra);
                        //     block->insertBefore(curr_inst, fused_inst);
                        //     block->insertBefore(curr_inst, mov_de);
                        //     block->insertBefore(curr_inst, mov_ene);

                        //     block->removeInst(curr_inst);
                        //     freeInsts.push_back(curr_inst);
                        //     block->removeInst(next_inst);
                        //     freeInsts.push_back(next_inst);
                        // }
                    }
                }
            }
        }
    }
}

// 跟op1不能合并（会相互影响）
void PeepholeOptimization::op2()
{

    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
        {
            auto block = *block_iter;
            if (block->getInsts().empty())
                continue;

            std::vector<MachineInstruction *> insts;
            insts.assign(block->begin(), block->end());
            auto curr_inst_iter = insts.begin();
            auto next_inst_iter = next(curr_inst_iter, 1);

            for (; next_inst_iter != insts.end(); curr_inst_iter++, next_inst_iter++)
            {
                auto curr_inst = *curr_inst_iter;
                auto next_inst = *next_inst_iter;

                if (curr_inst->isBinary() && next_inst->isMov())
                {
                    auto binary_def = curr_inst->getDef()[0];
                    auto mov_src = next_inst->getUse()[0];
                    if (*binary_def == *mov_src && next_inst->getCond() == MachineInstruction::NONE)
                    {
                        auto binary_src1 = curr_inst->getUse()[0];
                        auto binary_src2 = curr_inst->getUse()[1];
                        auto mov_dst = next_inst->getDef()[0];

                        auto new_inst = new BinaryMInstruction(block, curr_inst->getOpType(), new MachineOperand(*mov_dst), new MachineOperand(*binary_src1), new MachineOperand(*binary_src2));

                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isBinary() && next_inst->isVmov() && curr_inst->getDef()[0]->getValType()->isFloat())
                {
                    auto binary_def = curr_inst->getDef()[0];
                    auto mov_src = next_inst->getUse()[0];
                    if (*binary_def == *mov_src && next_inst->getCond() == MachineInstruction::NONE)
                    {
                        auto binary_src1 = curr_inst->getUse()[0];
                        auto binary_src2 = curr_inst->getUse()[1];
                        auto mov_dst = next_inst->getDef()[0];

                        auto new_inst = new BinaryMInstruction(block, curr_inst->getOpType(), new MachineOperand(*mov_dst), new MachineOperand(*binary_src1), new MachineOperand(*binary_src2));

                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isMov() && next_inst->isMov())
                {
                    if (*curr_inst->getDef()[0] == *next_inst->getUse()[0] && curr_inst->getCond() == MachineInstruction::NONE && next_inst->getCond() == MachineInstruction::NONE)
                    {
                        auto src1 = curr_inst->getUse()[0];
                        auto mov_dst = next_inst->getDef()[0];
                        auto new_inst = new MovMInstruction(block, MovMInstruction::MOV, new MachineOperand(*mov_dst), new MachineOperand(*src1));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isVmov() && next_inst->isVmov())
                {
                    if (*curr_inst->getDef()[0] == *next_inst->getUse()[0] && curr_inst->getCond() == MachineInstruction::NONE && next_inst->getCond() == MachineInstruction::NONE)
                    {
                        auto src1 = curr_inst->getUse()[0];
                        auto mov_dst = next_inst->getDef()[0];
                        auto new_inst = new MovMInstruction(block, MovMInstruction::VMOV, new MachineOperand(*mov_dst), new MachineOperand(*src1));
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isMovShift() && (next_inst->isAdd() || next_inst->isSub() || next_inst->isRsb()) && !next_inst->getDef()[0]->getValType()->isFloat())
                {
                    // 浮点没有移位
                    // mov v5, v2, lsl #2
                    // add v4, v3, v5 (add v4, v5, v3)
                    // --->
                    // mov v5, v2, lsl #2
                    // add v4, v3, v2, lsl #2

                    auto mov_dst = curr_inst->getDef()[0];
                    auto mov_src = curr_inst->getUse()[0];
                    auto mov_imm = curr_inst->getUse()[1];
                    auto bin_dst = next_inst->getDef()[0];
                    auto bin_src1 = next_inst->getUse()[0];
                    auto bin_src2 = next_inst->getUse()[1];

                    if (*mov_dst == *bin_src1 && *mov_dst == *bin_src2)
                        continue;

                    if (!bin_src1->isImm() && !bin_src2->isImm() &&
                        ((*mov_dst == *bin_src1 && next_inst->isAdd()) || *mov_dst == *bin_src2))
                    {
                        MachineOperand *tem_src = (*mov_dst == *bin_src1) ? bin_src2 : bin_src1;
                        int op;
                        switch (curr_inst->getOpType())
                        {
                        case MovMInstruction::MOVASR:
                            if (next_inst->isAdd())
                                op = BinaryMInstruction::ADDASR;
                            else if (next_inst->isSub())
                                op = BinaryMInstruction::SUBASR;
                            else
                                op = BinaryMInstruction::RSBASR;
                            break;
                        case MovMInstruction::MOVLSL:
                            if (next_inst->isAdd())
                                op = BinaryMInstruction::ADDLSL;
                            else if (next_inst->isSub())
                                op = BinaryMInstruction::SUBLSL;
                            else
                                op = BinaryMInstruction::RSBLSL;
                            break;
                        case MovMInstruction::MOVLSR:
                            if (next_inst->isAdd())
                                op = BinaryMInstruction::ADDLSR;
                            else if (next_inst->isSub())
                                op = BinaryMInstruction::SUBLSR;
                            else
                                op = BinaryMInstruction::RSBLSR;
                            break;
                        default:
                            assert(0);
                            break;
                        }
                        auto new_bin = new BinaryMInstruction(block, op, new MachineOperand(*bin_dst), new MachineOperand(*tem_src), new MachineOperand(*mov_src), new MachineOperand(*mov_imm));

                        if (!(*mov_dst == *mov_src))
                        {
                            block->insertBefore(next_inst, new_bin);
                            block->removeInst(next_inst);
                            freeInsts.push_back(next_inst);
                        }

                        else if (!(*bin_dst == *mov_src))
                        {
                            block->insertBefore(curr_inst, new_bin);
                            block->removeInst(next_inst);
                            freeInsts.push_back(next_inst);
                        }
                    }
                }
            }
        }
    }
}

// 窗口 = 3
void PeepholeOptimization::op3()
{

    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
        {
            auto block = *block_iter;
            if (block->getInsts().empty() || block->getInsts().size() <= 3)
                continue;

            std::vector<MachineInstruction *> insts;
            insts.assign(block->begin(), block->end());
            auto curr_inst_iter = insts.begin();
            auto second_inst_iter = next(curr_inst_iter, 1);
            auto third_inst_iter = next(second_inst_iter, 1);

            for (; third_inst_iter != insts.end(); curr_inst_iter++, second_inst_iter++, third_inst_iter++)
            {
                auto curr_inst = *curr_inst_iter;
                auto second_inst = *second_inst_iter;
                auto third_inst = *third_inst_iter;

                if (curr_inst->isAdd() && third_inst->isStore())
                {
                    // add vr7279, fp, #-12
                    // mov vr28908, #0
                    // str vr28908, [vr7279]
                    // --->
                    // add vr7279, fp, #-12
                    // mov vr28908, #0
                    // str vr28908, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (third_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *third_inst->getUse()[1] && curr_inst->getUse()[1]->isImm() && second_inst->getDef().size() == 1 &&
                        !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        if ((third_inst->getUse()[0]->getValType()->isFloat() && (curr_inst->getUse()[1]->isIllegalShifterOperand() || curr_inst->getUse()[1]->getVal() < -1023 || curr_inst->getUse()[1]->getVal() > 1023)) || (third_inst->getUse()[0]->getValType()->isInt() && (curr_inst->getUse()[1]->getVal() < -4095 || curr_inst->getUse()[1]->getVal() > 4095)))
                            continue;
                        auto new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }

                    else if (third_inst->getUse().size() == 2 &&
                             *curr_inst->getDef()[0] == *third_inst->getUse()[1] && (curr_inst->getUse()[1]->isReg() || curr_inst->getUse()[1]->isVReg()) && second_inst->getDef().size() == 1 &&
                             !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        if (third_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        auto new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]));
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }
                }

                else if (curr_inst->isSub() && third_inst->isStore())
                {
                    // sub vr7279, fp, #12
                    // mov vr28908, #0
                    // str vr28908, [vr7279]
                    // --->
                    // sub vr7279, fp, #12
                    // mov vr28908, #0
                    // str vr28908, [fp, #-12]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (third_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *third_inst->getUse()[1] && curr_inst->getUse()[1]->isImm() && second_inst->getDef().size() == 1 &&
                        !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        auto src_imm = new MachineOperand(MachineOperand::IMM, -1 * curr_inst->getUse()[1]->getVal(), TypeSystem::constIntType);
                        if ((third_inst->getUse()[0]->getValType()->isFloat() && (src_imm->isIllegalShifterOperand() || src_imm->getVal() < -1023 || src_imm->getVal() > 1023)) || (third_inst->getUse()[0]->getValType()->isInt() && (src_imm->getVal() < -4095 || src_imm->getVal() > 4095)))
                            continue;
                        auto new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), src_imm);
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }
                }

                else if (curr_inst->isAddShift() && third_inst->isStore())
                {
                    // add r4, r2, r1, LSL #2
                    // mov r3, #0
                    // str r3, [r4]
                    // --->
                    // add r4, r2, r1, LSL #2
                    // mov r3, #0
                    // str r3, [r2, r1, LSL #2]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (third_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *third_inst->getUse()[1] && curr_inst->getUse()[2]->isImm() && second_inst->getDef().size() == 1 &&
                        !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        if (third_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case BinaryMInstruction::ADDASR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STOREASR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STORELSR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSL:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STORELSL, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        }
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }
                }

                else if (curr_inst->isMovShift() && third_inst->isStore())
                {
                    // lsl r2, r1, #2
                    // mov r3, #0
                    // str r3, [r4, r2]
                    // --->
                    // lsl r2, r1, #2
                    // mov r3, #0
                    // str r3, [r4, r1, LSL #2]

                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (third_inst->getUse().size() == 3 &&
                        *curr_inst->getDef()[0] == *third_inst->getUse()[2] && curr_inst->getUse()[1]->isImm() && second_inst->getDef().size() == 1 &&
                        !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        if (third_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case MovMInstruction::MOVASR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*third_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STOREASR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*third_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STORELSR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSL:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*third_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STORELSL, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        }
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }
                }

                else if (curr_inst->isMovShift() && third_inst->isLoad())
                {
                    // lsl r3, r2, #2
                    // add r12, r5, r2, lsl #2
                    // ldr r3, [r5, r3]
                    // --->
                    // ldr r3, [r5, r2, lsl #2]
                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (third_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *third_inst->getUse()[1] && curr_inst->getUse()[1]->isImm() && second_inst->getDef().size() == 1 &&
                        !(*second_inst->getDef()[0] == *curr_inst->getDef()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[0]) && !(*second_inst->getDef()[0] == *curr_inst->getUse()[1]))
                    {
                        if (third_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case MovMInstruction::MOVASR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*third_inst->getDef()[0]), new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADASR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*third_inst->getDef()[0]), new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADLSR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSL:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*third_inst->getDef()[0]), new MachineOperand(*third_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADLSL, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        }
                        block->insertBefore(third_inst, new_inst);
                        block->removeInst(third_inst);
                        freeInsts.push_back(third_inst);
                    }
                }
            }
        }
    }
}

void PeepholeOptimization::op4()
{

    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;
        for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
        {
            auto block = *block_iter;
            if (block->getInsts().empty())
                continue;

            std::vector<MachineInstruction *> insts;
            insts.assign(block->begin(), block->end());
            auto curr_inst_iter = insts.begin();
            auto next_inst_iter = next(curr_inst_iter, 1);

            for (; next_inst_iter != insts.end(); curr_inst_iter++, next_inst_iter++)
            {
                auto curr_inst = *curr_inst_iter;
                auto next_inst = *next_inst_iter;

                if (curr_inst->isAddShift() && next_inst->isLoad())
                {
                    // add r7, r5, r6, LSL #2
                    // ldr r5, [r7]
                    // ​--->
                    // ​add r7, r5, r6, LSL #2
                    // ​ldr r5, [ r5, r6, LSL #2] （浮点不行）
                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 1 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[0] &&
                        curr_inst->getUse()[2]->isImm())
                    {
                        if (next_inst->getDef()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case BinaryMInstruction::ADDASR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), LoadMInstruction::LOADASR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), LoadMInstruction::LOADLSR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSL:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), LoadMInstruction::LOADLSL, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        }
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isAddShift() && next_inst->isStore())
                {
                    // add r7, r5, r6, LSL #2
                    // str r5, [r7]
                    // ​--->
                    // ​add r7, r5, r6, LSL #2
                    // ​str r5, [ r5, r6, LSL #2] （浮点不行）
                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0] || *curr_inst->getDef()[0] == *curr_inst->getUse()[1])
                        continue;

                    if (next_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[1] &&
                        curr_inst->getUse()[2]->isImm())
                    {
                        if (next_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case BinaryMInstruction::ADDASR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STOREASR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STORELSR, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        case BinaryMInstruction::ADDLSL:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[1]), StoreMInstruction::STORELSL, new MachineOperand(*curr_inst->getUse()[2]));
                            break;
                        }
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isMovShift() && next_inst->isLoad())
                {
                    //    lsl vr371, vr27, #2
                    //    ldr vr189, [vr181, vr371]
                    //    ->
                    //    lsl vr371, vr27, #2
                    //    ldr vr189, [vr181, vr27, lsl #2]
                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0])
                        continue;

                    if (next_inst->getUse().size() == 2 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[1] &&
                        curr_inst->getUse()[1]->isImm())
                    {
                        if (next_inst->getDef()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case MovMInstruction::MOVASR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADASR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSR:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADLSR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSL:
                            new_inst = new LoadMInstruction(block, new MachineOperand(*next_inst->getDef()[0]), new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*curr_inst->getUse()[0]), LoadMInstruction::LOADLSL, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        }
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }

                else if (curr_inst->isMovShift() && next_inst->isStore())
                {
                    // lsl r5, r3, #2
                    // str r4, [r6, r5]
                    // ->
                    // lsl r5, r3, #2
                    // str r4, [r6, r3, lsl #2]
                    if (*curr_inst->getDef()[0] == *curr_inst->getUse()[0])
                        continue;

                    if (next_inst->getUse().size() == 3 &&
                        *curr_inst->getDef()[0] == *next_inst->getUse()[2] &&
                        curr_inst->getUse()[1]->isImm())
                    {
                        if (next_inst->getUse()[0]->getValType()->isFloat())
                            continue;
                        MachineInstruction *new_inst;
                        switch (curr_inst->getOpType())
                        {
                        case MovMInstruction::MOVASR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*next_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STOREASR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSR:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*next_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STORELSR, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        case MovMInstruction::MOVLSL:
                            new_inst = new StoreMInstruction(block, new MachineOperand(*next_inst->getUse()[0]), new MachineOperand(*next_inst->getUse()[1]), new MachineOperand(*curr_inst->getUse()[0]), StoreMInstruction::STORELSL, new MachineOperand(*curr_inst->getUse()[1]));
                            break;
                        }
                        block->insertBefore(next_inst, new_inst);
                        block->removeInst(next_inst);
                        freeInsts.push_back(next_inst);
                    }
                }
            }
        }
    }
}

// // 按numerical顺序push还是WA，push窥孔优化似乎不行 https://developer.arm.com/documentation/dui0473/m/arm-and-thumb-instructions/push?lang=en
// void PeepholeOptimization::op4()
// {

//     for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
//     {
//         auto func = *func_iter;
//         for (auto block_iter = func->begin(); block_iter != func->end(); block_iter++)
//         {
//             auto block = *block_iter;
//             if (block->getInsts().empty())
//                 continue;

//             std::vector<MachineInstruction *> insts;
//             insts.assign(block->begin(), block->end());
//             auto curr_inst_iter = insts.begin();

//             for (; curr_inst_iter != insts.end(); curr_inst_iter++)
//             {
//                 auto curr_inst = *curr_inst_iter;
//                 if(curr_inst->isPush())
//                 {
//                     std::vector<MachineOperand *> tem_vec;
//                     std::vector<MachineInstruction *> old_insts;
//                     std::vector<MachineInstruction *> new_insts;
//                     auto tem_inst_iter = curr_inst_iter;
//                     int tc = 0;
//                     while ((*tem_inst_iter)->isPush())
//                     {
//                         old_insts.push_back(*tem_inst_iter);
//                         auto tem_inst = *tem_inst_iter;
//                         tem_vec.insert(tem_vec.end(), tem_inst->getUse().begin(), tem_inst->getUse().end());
//                         tem_inst_iter++;
//                         tc++;
//                     }
//                     curr_inst_iter = tem_inst_iter - 1;

//                     if(old_insts.size()==1) continue;
//                     // if(old_insts.size()>1)    printf("old_insts.size() = %d\n",old_insts.size());

//                     std::vector<std::vector<MachineOperand *>> result;
//                     int vectorSize = tem_vec.size();
//                     int lasti = 0;
//                     for (int i = 1; i < vectorSize; i ++)
//                     {
//                         if(tem_vec[i-1]->getReg() > tem_vec[i]->getReg())
//                         {
//                             result.push_back(std::vector<MachineOperand *>(tem_vec.begin() + lasti, tem_vec.begin() + i));
//                             lasti = i;
//                         }
//                     }
//                     if(lasti != vectorSize)
//                         result.push_back(std::vector<MachineOperand *>(tem_vec.begin() + lasti, tem_vec.begin() + vectorSize));

//                     if(result.size()>1)
//                     {
//                         // printf("size = %d\n",result.size());
//                         continue;
//                     }

//                     for(auto regs : result)
//                     {
//                         auto new_inst = new StackMInstruction(block, StackMInstruction::PUSH, regs);
//                         block->insertBefore(curr_inst, new_inst);
//                     }

//                     for(auto inst : old_insts)
//                     {
//                         block->removeInst(inst);
//                         freeInsts.push_back(inst);
//                     }
//                     // if(old_insts.size()>1)    printf("end --- \n");
//                 }
//             }
//         }
//     }
// }
