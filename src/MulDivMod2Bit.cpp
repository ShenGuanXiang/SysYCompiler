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
    div2mul();
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
using LL = long long;
const int N = 32;
inline int clz(int x) { return __builtin_clz(x); }
inline int ctz(int x) { return __builtin_ctz(x); }

struct Multiplier {
    long long m;
    int l;
};

Multiplier chooseMultiplier(int d, int p) {
    assert(d != 0);
    assert(p >= 1 && p <= N);
    int l = floor(log2(d));
    LL high = ceil((double(LL(1) << (l+32)) / d));
    return {high, l};
}


// void MulDivMod2Bit::div2mul()
// {
//     for(auto func_it = unit->begin(); func_it != unit->end(); func_it++)
//     {
//         auto func = *func_it;
//         for(auto bb :  func->getBlocks()){
//             if(!bb)
//                 continue;
//             std::vector<MachineInstruction*> insts;
//             insts.assign(bb->begin(), bb->end());
//             for(auto inst : insts){
//                 if(inst->isDiv() 
//                 && inst->getUse()[1]->getMDef()
//                 && inst->getUse()[1]->getMDef()->getUse()[0]->isImm()){
//                     int d = inst->getUse()[1]->getMDef()->getUse()[0]->getVal();
//                     int s = ctz(d);
//                     auto off1 = new MachineOperand(MachineOperand::IMM, 31);    
//                     auto tmp = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel()); 
//                     if(d == (int(1) << s)){
//                         // d = 2**n
//                         printf(" d = 2**n \n");
//                         MachineInstruction* inst1 = new MovMInstruction(bb, MovMInstruction::MOVLSR, tmp, inst->getUse()[0], off1);
//                         bb->insertBefore(inst, inst1);
//                         auto off = new MachineOperand(MachineOperand::IMM, s);                   
//                         MachineInstruction* inst2 = new MovMInstruction(bb, MovMInstruction::MOVASR, inst->getDef()[0], inst->getUse()[0], off);                   
//                         bb->insertBefore(inst, inst2);                        
//                         MachineInstruction* inst5 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, inst->getDef()[0], inst->getDef()[0], tmp);
//                         bb->insertBefore(inst, inst5);
//                         bb->remove(inst);
//                     }
//                     else {
                        
//                         int a = 0;
//                         if(d % (int(1) << s) == 0){
//                             d = d / (int(1) << s);
//                             a = s;
//                         }
//                         Multiplier multi = chooseMultiplier(d, N);
//                         // printf("m:%lld, l:%d\n", multi.m, multi.l+a);
//                         if(multi.m < (LL(1) << (N-1))){
//                             printf(" d != 2**n \n");
//                             auto m = new MachineOperand(MachineOperand::IMM, multi.m);
//                             auto l = new MachineOperand(MachineOperand::IMM, multi.l+a);
//                             auto rh = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
//                             auto rl = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
//                             MachineInstruction* inst1 = new LoadMInstruction(bb, tmp, m);
//                             MachineInstruction* inst2 = new SmullMInstruction(bb, rl, rh, inst->getUse()[0], tmp);
//                             MachineInstruction* inst3 = new MovMInstruction(bb, MovMInstruction::MOVASR, inst->getDef()[0], l);
//                             MachineInstruction* inst4 = new MovMInstruction(bb, MovMInstruction::MOVLSR, tmp, rh, off1);
//                             MachineInstruction* inst5 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, inst->getDef()[0], inst->getDef()[0], tmp);                      
//                             bb->insertBefore(inst, inst1);
//                             bb->insertBefore(inst, inst2);
//                             bb->insertBefore(inst, inst3);
//                             bb->insertBefore(inst, inst4);
//                             bb->insertBefore(inst, inst5);
//                             bb->remove(inst);
//                         }                 
//                     }
//                 }
//                 else if(inst->isMod()){
//                     int d = inst->getUse()[1]->getMDef()->getUse()[0]->getVal();
//                     if((d > 0) && ((d & (d-1)) == 0)){
//                         printf("use mod\n");
//                         auto off = new MachineOperand(MachineOperand::IMM, d-1);
//                         auto off1 = new MachineOperand(MachineOperand::IMM, 31);
//                         auto tmp = new MachineOperand( MachineOperand::VREG, SymbolTable::getLabel());
//                         MachineInstruction* inst1 = new BinaryMInstruction(nullptr, BinaryMInstruction::AND, inst->getDef()[0], inst->getUse()[0], off);
//                         MachineInstruction* inst2 = new MovMInstruction(nullptr, MovMInstruction::MOVLSR, tmp, inst->getUse()[0], off1);
//                         MachineInstruction* inst3 = new BinaryMInstruction(nullptr, BinaryMInstruction::ADD, inst->getDef()[0], inst->getDef()[0], tmp);
//                         bb->insertBefore(inst1, inst);
//                         bb->insertBefore(inst2, inst);
//                         bb->insertBefore(inst3, inst);
//                         bb->remove(inst);
//                     }
//                 }
//             }
//         }
//     }
// }








void MulDivMod2Bit::div2mul()
{

    for(auto func_it = unit->begin(); func_it != unit->end(); func_it++)
    {
        auto func = *func_it;
        for(auto bb :  func->getBlocks()){
            if(!bb)
                continue;
            std::vector<MachineInstruction*> insts;
            insts.assign(bb->begin(), bb->end());
            for(auto inst : insts){
                if(inst->isDiv() 
                && inst->getUse()[1]->getMDef()
                && inst->getUse()[1]->getMDef()->getUse()[0]->isImm()){

                    int d = inst->getUse()[1]->getMDef()->getUse()[0]->getVal();
                    int s = ctz(d);

                    if(d == (int(1) << s) || d == -(int(1) << s)){

                        auto off1 = new MachineOperand(MachineOperand::IMM, (int(1) << s) - 1 );    // 改成2^s -1
                        auto off = new MachineOperand(MachineOperand::IMM, s); 
                        auto new_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel()); 
                        auto asr_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel()); 

                        MachineInstruction* inst1 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new_op, inst->getUse()[0], off1); // a+2^s-1

                        MachineInstruction* inst2 = new CmpMInstruction(bb, inst->getUse()[0], new MachineOperand(MachineOperand::IMM, 0));  // cmp a #0

                        MachineInstruction* inst3 = new MovMInstruction(bb, MovMInstruction::MOV, asr_op, new_op, nullptr, MovMInstruction::LT); // movlt

                        MachineInstruction* inst4 = new MovMInstruction(bb, MovMInstruction::MOV, asr_op, inst->getUse()[0], nullptr, MovMInstruction::GE); // movlt

                        MachineInstruction* inst5 = new MovMInstruction(bb, MovMInstruction::MOVASR, inst->getDef()[0], asr_op, off);                   

                        bb->insertBefore(inst, inst1);
                        bb->insertBefore(inst, inst2);
                        bb->insertBefore(inst, inst3);
                        bb->insertBefore(inst, inst4);
                        bb->insertBefore(inst, inst5);

                        if(d == -(int(1) << s)) //负数
                        {
                            // 结果再取反
                            MachineInstruction* inst6 = new BinaryMInstruction(bb, BinaryMInstruction::RSB, inst->getDef()[0],  inst->getDef()[0], new MachineOperand(MachineOperand::IMM, 0)); 
                            bb->insertBefore(inst, inst6);
                        }
                        bb->remove(inst);

                    }
                    else {
                        int a = 0;
                        if(d % (int(1) << s) == 0){
                            d = d / (int(1) << s);
                            a = s;
                        }
                        Multiplier multi = chooseMultiplier(d, N);
                        // printf("m:%lld, l:%d\n", multi.m, multi.l+a);
                        if(multi.m < (LL(1) << (N-1))){

                            auto off1 = new MachineOperand(MachineOperand::IMM, 31);    
                            auto tmp = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel()); 
                            auto m = new MachineOperand(MachineOperand::IMM, multi.m);
                            auto l = new MachineOperand(MachineOperand::IMM, multi.l+a);
                            auto rh = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                            auto rl = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                            MachineInstruction* inst1 = new LoadMInstruction(bb, tmp, m);
                            MachineInstruction* inst2 = new SmullMInstruction(bb, rl, rh, inst->getUse()[0], tmp);
                            MachineInstruction* inst3 = new MovMInstruction(bb, MovMInstruction::MOVASR, inst->getDef()[0], l);
                            MachineInstruction* inst4 = new MovMInstruction(bb, MovMInstruction::MOVLSR, tmp, rh, off1);
                            MachineInstruction* inst5 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, inst->getDef()[0], inst->getDef()[0], tmp);                      
                            bb->insertBefore(inst, inst1);
                            bb->insertBefore(inst, inst2);
                            bb->insertBefore(inst, inst3);
                            bb->insertBefore(inst, inst4);
                            bb->insertBefore(inst, inst5);
                            bb->remove(inst);
                        }
                        
                    }
                
                }
                else if(inst->isMod()){
                    int d = inst->getUse()[1]->getMDef()->getUse()[0]->getVal();
                    if((d > 0) && ((d & (d-1)) == 0)){
                        auto off = new MachineOperand(MachineOperand::IMM, d-1);
                        auto off1 = new MachineOperand(MachineOperand::IMM, 31);
                        auto tmp = new MachineOperand( MachineOperand::VREG, SymbolTable::getLabel());
                        MachineInstruction* inst1 = new BinaryMInstruction(nullptr, BinaryMInstruction::AND, inst->getDef()[0], inst->getUse()[0], off);
                        MachineInstruction* inst2 = new MovMInstruction(nullptr, MovMInstruction::MOVLSR, tmp, inst->getUse()[0], off1);
                        MachineInstruction* inst3 = new BinaryMInstruction(nullptr, BinaryMInstruction::ADD, inst->getDef()[0], inst->getDef()[0], tmp);
                        bb->insertBefore(inst1, inst);
                        bb->insertBefore(inst2, inst);
                        bb->insertBefore(inst3, inst);
                        bb->remove(inst);
                    }
                }
            }
        }
    }

}


// 还剩：2^s-1立即数超限（拆分 or load）
//       div的else（choosemul）情况下，处理 除数为负数的情况
//       mul转移位时，处理 -2^n 的情况