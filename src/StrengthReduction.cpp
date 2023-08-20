#include "StrengthReduction.h"
#include "DeadCodeElim.h"
#include <cmath>

using namespace std;

static std::set<MachineInstruction *> freeInsts;
static std::set<MachineOperand> multi_def;

using LL = long long;
const int N = 32;
inline int clz(int x) { return __builtin_clz(x); }
inline int ctz(int x) { return __builtin_ctz(x); }



struct Multiplier
{
    long long m;
    int l;
};

Multiplier chooseMultiplier(int d, int p)
{
    assert(d != 0);
    assert(p >= 1 && p <= N);
    int l = floor(log2(d));
    LL high = ceil((double(LL(1) << (l + 32)) / d));
    return {high, l};
}

union VAL
{
    unsigned unsigned_val;
    signed signed_val;
    float float_val;
};

void StrengthReduction::pass()
{
    for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
    {
        auto func = *func_iter;

        // find multi_defined ops
        multi_def.clear();
        std::set<MachineOperand> defs = std::set<MachineOperand>();
        for (auto bb : func->getBlocks())
            for (auto inst : bb->getInsts())
                for (auto def : inst->getDef())
                {
                    if (defs.count(*def))
                        multi_def.insert(*def);
                    else
                        defs.insert(*def);
                }

        // Compute Domtree
        domtree.clear();
        func->computeDom();
        for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
        {
            MachineBlock *bb = *it_bb;
            domtree[bb->getIDom()].push_back(bb);
        }

        // 整型操作优化
        std::map<MachineOperand, int> int_op2val;
        dfs(func->getEntry(), int_op2val);
        // 浮点操作优化
        std::map<MachineOperand, float> float_op2val;
        dfs(func->getEntry(), float_op2val);
    }

    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();

    MachineDeadCodeElim mdce(unit);
    mdce.pass(false); // 死代码消除

    // div2mul();
    // mul2lsl(); // 乘法 to 移位
}

// 整型操作优化
void StrengthReduction::dfs(MachineBlock *bb, std::map<MachineOperand, int> op2val)
{
    auto insts = bb->getInsts();
    std::set<MachineOperand> multi_def_ops = std::set<MachineOperand>();
    for (auto inst : insts)
    {
        for (auto def : inst->getDef())
            if (op2val.count(*def))
            {
                assert(multi_def_ops.count(*def));
                op2val.erase(*def);
            };

        // 存值，只处理无条件赋值&(SSA||当前块定义)的情况
        if (inst->isLoad())
        {
            if (bb->getParent()->getAdditionalArgsOffset().count(inst->getUse()[0]))
                continue;
            // 暂不考虑一次load多个寄存器的情况
            if (inst->getUse().size() == 1 && inst->getDef()[0]->getValType()->isInt() &&
                inst->getCond() == MachineInstruction::NONE && inst->getUse()[0]->isImm())
            {
                if (inst->getUse()[0]->getValType()->isInt())
                    op2val[*inst->getDef()[0]] = (int)inst->getUse()[0]->getVal();
                else
                {
                    assert(inst->getUse()[0]->getValType()->isFloat());
                    VAL val;
                    val.float_val = (float)inst->getUse()[0]->getVal();
                    op2val[*inst->getDef()[0]] = val.signed_val;
                }
                if (multi_def.count(*inst->getDef()[0]))
                    multi_def_ops.insert(*inst->getDef()[0]);
            }
            // vldr.32和ldr的立即数偏移范围不同，还没确定
            // if (inst->getUse().size() == 2 && op2val.count(*inst->getUse()[1]) && isSignedShifterOperandVal(op2val[*inst->getUse()[1]]))
            // {
            //     inst->getUse()[1] = new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[1]]);
            //     inst->getUse()[1]->setParent(inst);
            // }
        }
        else if (inst->isMov() || inst->isVmov())
        {
            if (bb->getParent()->getAdditionalArgsOffset().count(inst->getUse()[0]))
                continue;
            // 暂不考虑带移位的情况
            if (inst->getDef()[0]->getValType()->isInt() && op2val.count(*inst->getUse()[0]) && isSignedShifterOperandVal(op2val[*inst->getUse()[0]]))
            {
                inst->getUse()[0] = new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[0]]);
                inst->getUse()[0]->setParent(inst);
            }
            if (inst->getUse().size() == 1 && inst->getDef()[0]->getValType()->isInt() &&
                inst->getCond() == MachineInstruction::NONE && inst->getUse()[0]->isImm())
            {
                if (inst->getUse()[0]->getValType()->isInt())
                    op2val[*inst->getDef()[0]] = (int)inst->getUse()[0]->getVal();
                else
                {
                    assert(inst->getUse()[0]->getValType()->isFloat());
                    VAL val;
                    val.float_val = (float)inst->getUse()[0]->getVal();
                    op2val[*inst->getDef()[0]] = val.signed_val;
                }
                if (multi_def.count(*inst->getDef()[0]))
                    multi_def_ops.insert(*inst->getDef()[0]);
            }
            else if (inst->getUse().size() == 1 && inst->getDef()[0]->getValType()->isInt() &&
                     inst->getCond() == MachineInstruction::NONE && op2val.count(*inst->getUse()[0]))
            {
                op2val[*inst->getDef()[0]] = op2val[*inst->getUse()[0]];
                if (multi_def.count(*inst->getDef()[0]))
                    multi_def_ops.insert(*inst->getDef()[0]);
            }
        }

        else if (inst->isAdd() && inst->getDef()[0]->getValType()->isInt() && inst->getUse().size() == 2)
        {
            if (op2val.count(*inst->getUse()[0]) && (op2val.count(*inst->getUse()[1]) || inst->getUse()[1]->isImm()))
            {
                assert(!inst->getUse()[0]->isImm());
                int val1 = op2val[*inst->getUse()[0]];
                int val2 = op2val.count(*inst->getUse()[1]) ? op2val[*inst->getUse()[1]] : inst->getUse()[1]->getVal();
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto ldrInst = new LoadMInstruction(bb, dst, new MachineOperand(MachineOperand::IMM, val1 + val2));
                op2val[*dst] = val1 + val2;
                bb->insertBefore(inst, ldrInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[0]))
            {
                if (op2val[*inst->getUse()[0]] == 0)
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto MovInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(*inst->getUse()[1]));
                    bb->insertBefore(inst, MovInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else if (isSignedShifterOperandVal(op2val[*inst->getUse()[0]]))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto AddInst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[0]]));
                    bb->insertBefore(inst, AddInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else if (isSignedShifterOperandVal(-op2val[*inst->getUse()[0]]))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto SubInst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, -op2val[*inst->getUse()[0]]));
                    bb->insertBefore(inst, SubInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
            }
            else if ((op2val.count(*inst->getUse()[1]) && op2val[*inst->getUse()[1]] == 0) || (inst->getUse()[1]->isImm() && inst->getUse()[1]->getVal() == 0))
            {
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto MovInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(*inst->getUse()[0]));
                bb->insertBefore(inst, MovInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[1]))
            {
                if (isSignedShifterOperandVal(op2val[*inst->getUse()[1]]))
                {
                    inst->getUse()[1] = new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[1]]);
                    inst->getUse()[1]->setParent(inst);
                }
                else if (isSignedShifterOperandVal(-op2val[*inst->getUse()[1]]))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto SubInst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, -op2val[*inst->getUse()[1]]));
                    bb->insertBefore(inst, SubInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
            }
        }

        else if (inst->isSub() && inst->getDef()[0]->getValType()->isInt() && inst->getUse().size() == 2)
        {
            if (op2val.count(*inst->getUse()[0]) && (op2val.count(*inst->getUse()[1]) || inst->getUse()[1]->isImm()))
            {
                assert(!inst->getUse()[0]->isImm());
                int val1 = op2val[*inst->getUse()[0]];
                int val2 = op2val.count(*inst->getUse()[1]) ? op2val[*inst->getUse()[1]] : inst->getUse()[1]->getVal();
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto ldrInst = new LoadMInstruction(bb, dst, new MachineOperand(MachineOperand::IMM, val1 - val2));
                op2val[*dst] = val1 - val2;
                bb->insertBefore(inst, ldrInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[0]))
            {
                if (op2val[*inst->getUse()[0]] == 0)
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto NegInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, 0));
                    bb->insertBefore(inst, NegInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else if (isShifterOperandVal(op2val[*inst->getUse()[0]]))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto RsbInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[0]]));
                    bb->insertBefore(inst, RsbInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
            }
            else if ((op2val.count(*inst->getUse()[1]) && op2val[*inst->getUse()[1]] == 0) || (inst->getUse()[1]->isImm() && inst->getUse()[1]->getVal() == 0))
            {
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto MovInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(*inst->getUse()[0]));
                bb->insertBefore(inst, MovInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[1]))
            {
                if (isSignedShifterOperandVal(op2val[*inst->getUse()[1]]))
                {
                    inst->getUse()[1] = new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[1]]);
                    inst->getUse()[1]->setParent(inst);
                }
                else if (isSignedShifterOperandVal(-op2val[*inst->getUse()[1]]))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto AddInst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, -op2val[*inst->getUse()[1]]));
                    bb->insertBefore(inst, AddInst);
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
            }
        }

        else if (inst->isRsb() && inst->getDef()[0]->getValType()->isInt())
        {
            if (op2val.count(*inst->getUse()[0]) && (op2val.count(*inst->getUse()[1]) || inst->getUse()[1]->isImm()))
            {
                assert(!inst->getUse()[0]->isImm());
                int val1 = op2val[*inst->getUse()[0]];
                int val2 = op2val.count(*inst->getUse()[1]) ? op2val[*inst->getUse()[1]] : inst->getUse()[1]->getVal();
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto ldrInst = new LoadMInstruction(bb, dst, new MachineOperand(MachineOperand::IMM, val2 - val1));
                op2val[*dst] = val2 - val1;
                bb->insertBefore(inst, ldrInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[0]) && op2val[*inst->getUse()[0]] == 0)
            {
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto MovInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(*inst->getUse()[1]));
                bb->insertBefore(inst, MovInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if ((op2val.count(*inst->getUse()[1]) && isSignedShifterOperandVal(op2val[*inst->getUse()[1]])) || (inst->getUse()[1]->isImm() && isSignedShifterOperandVal(inst->getUse()[1]->getVal())))
            {
                inst->getUse()[1] = new MachineOperand(MachineOperand::IMM, op2val.count(*inst->getUse()[1]) ? op2val[*inst->getUse()[1]] : inst->getUse()[1]->getVal());
                inst->getUse()[1]->setParent(inst);
            }
        }

        // mul2lsl
        else if (inst->isMul())
        {
            if (op2val.count(*inst->getUse()[0]) && op2val.count(*inst->getUse()[1]))
            {
                assert(inst->getDef()[0]->getValType()->isInt());
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto ldrInst = new LoadMInstruction(bb, dst, new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[0]] * op2val[*inst->getUse()[1]]));
                op2val[*dst] = op2val[*inst->getUse()[0]] * op2val[*inst->getUse()[1]];
                bb->insertBefore(inst, ldrInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }
            else if (op2val.count(*inst->getUse()[0]))
            {
                assert(inst->getDef()[0]->getValType()->isInt());
                int m = op2val[*inst->getUse()[0]];
                int s = ctz(m);
                if (m == (int(1) << s) || m == -(int(1) << s))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    if (m == -1)
                    {
                        auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, 0));
                        bb->insertBefore(inst, negInst);
                    }
                    else
                    {
                        auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, s));
                        bb->insertBefore(inst, mov_lsl);
                        if (m == -(int(1) << s))
                        {
                            auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                            bb->insertBefore(inst, negInst);
                        }
                    }
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else
                {
                    if (m == 0) // 0是特例
                    {
                        auto dst = new MachineOperand(*inst->getDef()[0]);
                        auto movInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(MachineOperand::IMM, 0));
                        bb->insertBefore(inst, movInst);
                        bb->removeInst(inst);
                        freeInsts.insert(inst);
                    }
                    // TODO：非2的幂次
                    else
                    {
                        // 2^s - 1
                        s = ctz(m + 1);
                        if(m + 1 == (int(1) << s))
                        {
                            
                            // r2 = r1*31                            
                            // lsl r2, r1, #5
                            // sub r2, r2, r1
                            auto dst = new MachineOperand(*inst->getDef()[0]);

                            auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, s));
                            bb->insertBefore(inst, mov_lsl);
                            auto sub_inst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[1]));
                            bb->insertBefore(inst, sub_inst);
                            
                               
                            bb->removeInst(inst);
                            freeInsts.insert(inst); 
                        }
                        else if(m + 1 == -(int(1) << s))
                        {
                            // r2 = r1 * -33                     
                            // lsl r2, r1, #5
                            // add r2, r2, r1
                            // neg r2, r2

                            auto dst = new MachineOperand(*inst->getDef()[0]);

                            auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, s));
                            bb->insertBefore(inst, mov_lsl);
                            auto add_inst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[1]));
                            bb->insertBefore(inst, add_inst);
        
                            auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                            bb->insertBefore(inst, negInst);

                            bb->removeInst(inst);
                            freeInsts.insert(inst);   


                        }

                        else
                        {
                            s = ctz(m - 1);
                            if(m - 1 == (int(1) << s))
                            {

                                // r2 = r1*33                            
                                // lsl r2, r1, #5
                                // add r2, r2, r1

                                auto dst = new MachineOperand(*inst->getDef()[0]);

                                auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, s));
                                bb->insertBefore(inst, mov_lsl);
                                auto add_inst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[1]));
                                bb->insertBefore(inst, add_inst);
           
                                bb->removeInst(inst);
                                freeInsts.insert(inst);   
                     
                            }
                            else if (m - 1 == -(int(1) << s))
                            {
                                // r2 = r1 * -31                           
                                // lsl r2, r1, #5
                                // sub r2, r2, r1
                                // neg r2, r2

                                auto dst = new MachineOperand(*inst->getDef()[0]);

                                auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[1]), new MachineOperand(MachineOperand::IMM, s));
                                bb->insertBefore(inst, mov_lsl);
                                auto sub_inst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[1]));
                                bb->insertBefore(inst, sub_inst);

                                auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                                bb->insertBefore(inst, negInst);
                            
                                bb->removeInst(inst);
                                freeInsts.insert(inst); 

                            }
                        }
                    }

                }
            }
            else if (op2val.count(*inst->getUse()[1]))
            {
                assert(inst->getDef()[0]->getValType()->isInt());
                int m = op2val[*inst->getUse()[1]];
                int s = ctz(m);
                if (m == (int(1) << s) || m == -(int(1) << s))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    if (m == -1)
                    {
                        auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, 0));
                        bb->insertBefore(inst, negInst);
                    }
                    else
                    {
                        auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, s));
                        bb->insertBefore(inst, mov_lsl);
                        if (m == -(int(1) << s))
                        {
                            auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                            bb->insertBefore(inst, negInst);
                        }
                    }
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else
                {
                    if (m == 0) // 0是特例
                    {
                        auto dst = new MachineOperand(*inst->getDef()[0]);
                        auto movInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, new MachineOperand(MachineOperand::IMM, 0));
                        bb->insertBefore(inst, movInst);
                        bb->removeInst(inst);
                        freeInsts.insert(inst);
                    }
                    // TODO：非2的幂次
                    else
                    {
                        s = ctz(m + 1);
                        if(m + 1 == (int(1) << s))
                        {
                            auto dst = new MachineOperand(*inst->getDef()[0]);

                            auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, s));
                            bb->insertBefore(inst, mov_lsl);
                            auto sub_inst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[0]));
                            bb->insertBefore(inst, sub_inst);
                            
                               
                            bb->removeInst(inst);
                            freeInsts.insert(inst); 
                        }
                        else if(m + 1 == -(int(1) << s))
                        {

                            auto dst = new MachineOperand(*inst->getDef()[0]);

                            auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, s));
                            bb->insertBefore(inst, mov_lsl);
                            auto add_inst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[0]));
                            bb->insertBefore(inst, add_inst);
        
                            auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                            bb->insertBefore(inst, negInst);

                            bb->removeInst(inst);
                            freeInsts.insert(inst);   
                        }

                        else
                        {
                            s = ctz(m - 1);
                            if(m - 1 == (int(1) << s))
                            {
                                auto dst = new MachineOperand(*inst->getDef()[0]);

                                auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, s));
                                bb->insertBefore(inst, mov_lsl);
                                auto add_inst = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[0]));
                                bb->insertBefore(inst, add_inst);
           
                                bb->removeInst(inst);
                                freeInsts.insert(inst);   

                            }
                            else if (m - 1 == -(int(1) << s))
                            {
                                auto dst = new MachineOperand(*inst->getDef()[0]);

                                auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, dst, new MachineOperand(*inst->getUse()[0]), new MachineOperand(MachineOperand::IMM, s));
                                bb->insertBefore(inst, mov_lsl);
                                auto sub_inst = new BinaryMInstruction(bb, BinaryMInstruction::SUB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(*inst->getUse()[0]));
                                bb->insertBefore(inst, sub_inst);

                                auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                                bb->insertBefore(inst, negInst);
                            
                                bb->removeInst(inst);
                                freeInsts.insert(inst); 

                            }
                        }
                    }
                }
            }
        }

        // mod2and、div2asr
        else if (inst->isDiv())
        {
            if (op2val.count(*inst->getUse()[0]) && op2val.count(*inst->getUse()[1]))
            {
                assert(inst->getDef()[0]->getValType()->isInt());
                auto dst = new MachineOperand(*inst->getDef()[0]);
                auto ldrInst = new LoadMInstruction(bb, dst, new MachineOperand(MachineOperand::IMM, op2val[*inst->getUse()[0]] / op2val[*inst->getUse()[1]]));
                op2val[*dst] = op2val[*inst->getUse()[0]] / op2val[*inst->getUse()[1]];
                bb->insertBefore(inst, ldrInst);
                bb->removeInst(inst);
                freeInsts.insert(inst);
            }











            // 取模会翻译为div、mul、sub
            bool isMod = false;
            MachineInstruction *nxt = bb->getNext(inst), *nxt_nxt = nullptr;
            if (nxt != nullptr && nxt->isMul() && *inst->getDef()[0] == *nxt->getUse()[0] && *inst->getUse()[1] == *nxt->getUse()[1])
            {
                nxt_nxt = bb->getNext(nxt);
                if (nxt_nxt != nullptr && nxt_nxt->isSub() && *inst->getUse()[0] == *nxt_nxt->getUse()[0] && *nxt->getDef()[0] == *nxt_nxt->getUse()[1])
                {
                    isMod = true;
                }
            }
            if (isMod && op2val.count(*inst->getUse()[1]))
            {
                ; // TODO：mod2and
            }















            else if (op2val.count(*inst->getUse()[1]))
            {
                assert(inst->getDef()[0]->getValType()->isInt());
                int d = op2val[*inst->getUse()[1]];
                int s = ctz(d);
                if (d == (int(1) << s) || d == -(int(1) << s))
                {
                    auto dst = new MachineOperand(*inst->getDef()[0]);
                    auto dividend = new MachineOperand(*inst->getUse()[0]);
                    if (d == 1)
                    {
                        auto movInst = new MovMInstruction(bb, MovMInstruction::MOV, dst, dividend);
                        bb->insertBefore(inst, movInst);
                    }
                    else if (d == -1)
                    {
                        auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, dst, dividend, new MachineOperand(MachineOperand::IMM, 0));
                        bb->insertBefore(inst, negInst);
                    }
                    else
                    {
                        if (isSignedShifterOperandVal((int(1) << s) - 1))
                        {
                            auto off = new MachineOperand(MachineOperand::IMM, (int(1) << s) - 1); // 2^s -1
                            auto new_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
                            auto asr_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());

                            auto *inst1 = new CmpMInstruction(bb, dividend, new MachineOperand(MachineOperand::IMM, 0));                                                              // cmp a #0
                            auto *inst2 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new_op, new MachineOperand(*dividend), off);                                            // a+2^s-1
                            auto *inst3 = new MovMInstruction(bb, MovMInstruction::MOV, asr_op, new MachineOperand(*new_op), nullptr, MachineInstruction::LT);                        // movlt
                            auto *inst4 = new MovMInstruction(bb, MovMInstruction::MOV, new MachineOperand(*asr_op), new MachineOperand(*dividend), nullptr, MachineInstruction::GE); // movge
                            auto *inst5 = new MovMInstruction(bb, MovMInstruction::MOVASR, dst, new MachineOperand(*asr_op), new MachineOperand(MachineOperand::IMM, s));             // movasr

                            bb->insertBefore(inst, inst1);
                            bb->insertBefore(inst, inst2);
                            bb->insertBefore(inst, inst3);
                            bb->insertBefore(inst, inst4);
                            bb->insertBefore(inst, inst5);

                            if (d == -(int(1) << s))
                            {
                                auto negInst = new BinaryMInstruction(bb, BinaryMInstruction::RSB, new MachineOperand(*dst), new MachineOperand(*dst), new MachineOperand(MachineOperand::IMM, 0));
                                bb->insertBefore(inst, negInst);
                            }
                        }
                        else
                        {
                            // TODO: 2^s-1为非法立即数,<32种情况
                            continue;
                        }
                    }
                    bb->removeInst(inst);
                    freeInsts.insert(inst);
                }
                else
                {
                    assert(d != 0);
                    // TODO：非2的幂次
                }
            }
        }
    }

    for (auto op : multi_def_ops)
        op2val.erase(op);

    for (auto it_child = domtree[bb].begin(); it_child != domtree[bb].end(); it_child++)
        dfs(*it_child, op2val);
}

// 浮点操作优化
void StrengthReduction::dfs(MachineBlock *bb, std::map<MachineOperand, float> op2val)
{
    // TODO
    return;
}

// inline bool is2Exp(int val)
// {
//     return !(val & (val - 1));
// }

// void StrengthReduction::mul2lsl()
// {
//     for (auto func_iter = unit->begin(); func_iter != unit->end(); func_iter++)
//     {
//         auto func = *func_iter;
//         for (auto bb : func->getBlocks())
//         {
//             if (bb->getInsts().empty())
//                 continue;
//             auto curr_inst_iter = bb->begin();
//             auto next_inst_iter = next(curr_inst_iter, 1);
//             for (; next_inst_iter != bb->end(); curr_inst_iter++, next_inst_iter++)
//             {
//                 auto curr_inst = *curr_inst_iter;
//                 auto next_inst = *next_inst_iter;

//                 // example:
//                 // ldr r5, #2
//                 // mul r6, r4, r5
//                 // -------->
//                 // mov r6, r4, LSL#1

//                 if ((curr_inst->isMov() || (curr_inst->isLoad() && ((LoadMInstruction *)curr_inst)->is_1_src())) && next_inst->isMul())
//                 {
//                     auto Dst = curr_inst->getDef()[0]; // r5
//                     auto Src = curr_inst->getUse()[0]; // #2

//                     auto mulDst = next_inst->getDef()[0];  // r6
//                     auto mulSrc1 = next_inst->getUse()[0]; // r4
//                     auto mulSrc2 = next_inst->getUse()[1]; // r5

//                     // 判断 mov/load指令的目的操作数 是否属于两个乘法操作数之一，例如example里的 ldr r5, #2 和 mul r6, r4, r5 都有 r5
//                     if ((*mulSrc1 == *Dst || *mulSrc2 == *Dst) && Src->isImm() && !Src->getValType()->isFloat() && Src->getVal() != 0 && is2Exp(Src->getVal()))
//                     {
//                         assert(curr_inst->getCond() == MachineInstruction::NONE);
//                         // 让移位操作数等于另一个（不在mov/load里的）乘法操作数
//                         MachineOperand *lslSrc = (*mulSrc1 == *Dst) ? mulSrc2 : mulSrc1;                                                                               // r4
//                         auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, mulDst, lslSrc, new MachineOperand(MachineOperand::IMM, log2(Src->getVal()))); // mov r6, r4, LSL#1

//                         freeInsts.insert(next_inst);
//                         *(next_inst_iter) = mov_lsl;
//                     }
//                 }

//                 // example:
//                 // mov r6, #2
//                 // mov r8, #4
//                 // mul r9, r6, r8
//                 // -------->
//                 // mov r8, #4
//                 // mov r9, r8, LSL#1

//                 if (next_inst_iter + 1 != bb->end())
//                 {
//                     auto next_next_inst = *(next_inst_iter + 1);
//                     if ((curr_inst->isMov() || (curr_inst->isLoad() && ((LoadMInstruction *)curr_inst)->is_1_src())) && next_next_inst->isMul())
//                     {
//                         auto Dst = curr_inst->getDef()[0]; // r6
//                         auto Src = curr_inst->getUse()[0]; // #2

//                         auto mulDst = next_next_inst->getDef()[0];  // r9
//                         auto mulSrc1 = next_next_inst->getUse()[0]; // r6
//                         auto mulSrc2 = next_next_inst->getUse()[1]; // r8

//                         // 判断 mov/load指令的目的操作数 是否属于 两个乘法操作数之，例如example里的 mov r6, #2 和 mul r9, r6, r8 都有 r6
//                         if ((*mulSrc1 == *Dst || *mulSrc2 == *Dst) && Src->isImm() && !Src->getValType()->isFloat() && Src->getVal() != 0 && is2Exp(Src->getVal()))
//                         {
//                             assert(curr_inst->getCond() == MachineInstruction::NONE);
//                             // 让移位操作数等于另一个（不在mov/load里的）乘法操作数
//                             MachineOperand *lslSrc = (*mulSrc1 == *Dst) ? mulSrc2 : mulSrc1;                                                                               // = mulSrc2 = r8
//                             auto mov_lsl = new MovMInstruction(bb, MovMInstruction::MOVLSL, mulDst, lslSrc, new MachineOperand(MachineOperand::IMM, log2(Src->getVal()))); // mov r9, r8, LSL#1

//                             freeInsts.insert(next_next_inst);
//                             *(next_inst_iter + 1) = mov_lsl;
//                         }
//                     }
//                 }
//             }
//         }
//     }

//     for (auto inst : freeInsts)
//         delete inst;
//     freeInsts.clear();
// }

// void StrengthReduction::div2mul()
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

//                     if(d == (int(1) << s) || d == -(int(1) << s)){

//                         auto off1 = new MachineOperand(MachineOperand::IMM, (int(1) << s) - 1 );    // 改成2^s -1
//                         auto off = new MachineOperand(MachineOperand::IMM, s);
//                         auto new_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
//                         auto asr_op = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());

//                         MachineInstruction* inst1 = new BinaryMInstruction(bb, BinaryMInstruction::ADD, new_op, inst->getUse()[0], off1); // a+2^s-1

//                         MachineInstruction* inst2 = new CmpMInstruction(bb, inst->getUse()[0], new MachineOperand(MachineOperand::IMM, 0));  // cmp a #0

//                         MachineInstruction* inst3 = new MovMInstruction(bb, MovMInstruction::MOV, asr_op, new_op, nullptr, MovMInstruction::LT); // movlt

//                         MachineInstruction* inst4 = new MovMInstruction(bb, MovMInstruction::MOV, asr_op, inst->getUse()[0], nullptr, MovMInstruction::GE); // movlt

//                         MachineInstruction* inst5 = new MovMInstruction(bb, MovMInstruction::MOVASR, inst->getDef()[0], asr_op, off);

//                         bb->insertBefore(inst, inst1);
//                         bb->insertBefore(inst, inst2);
//                         bb->insertBefore(inst, inst3);
//                         bb->insertBefore(inst, inst4);
//                         bb->insertBefore(inst, inst5);

//                         if(d == -(int(1) << s)) //负数
//                         {
//                             // 结果再取反
//                             MachineInstruction* inst6 = new BinaryMInstruction(bb, BinaryMInstruction::RSB, inst->getDef()[0],  inst->getDef()[0], new MachineOperand(MachineOperand::IMM, 0));
//                             bb->insertBefore(inst, inst6);
//                         }
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

//                             auto off1 = new MachineOperand(MachineOperand::IMM, 31);
//                             auto tmp = new MachineOperand(MachineOperand::VREG, SymbolTable::getLabel());
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
//     for (auto inst : freeInsts)
//         delete inst;
//     freeInsts.clear();
// }

// 还剩：2^s-1立即数超限（拆分 or load）
//       div的else（choosemul）情况下，处理 除数为负数的情况
//       mod