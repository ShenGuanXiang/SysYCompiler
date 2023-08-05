#include "SparseCondConstProp.h"
#include "SimplifyCFG.h"

void Instruction::replaceUsesWith(Operand *old_op, Operand *new_op, BasicBlock *pre_bb)
{
    assert(this->isPHI() || pre_bb == nullptr);
    auto &uses = this->getUses();
    if (this->isPHI())
    {
        auto &srcs = ((PhiInstruction *)this)->getSrcs();
        int cnt = 0;
        for (auto &src : srcs)
        {
            if (src.second == old_op && (pre_bb == nullptr || src.first == pre_bb))
            {
                src.second = new_op;
                cnt++;
            }
        }
        for (auto &use : uses)
        {
            if (use == old_op)
            {
                bool used = false;
                for (auto &src : srcs)
                {
                    if (src.second == old_op)
                    {
                        used = true;
                        break;
                    }
                }
                if (!used)
                {
                    use->removeUse(this);
                }
                if (cnt)
                {
                    new_op->addUse(this);
                    use = new_op;
                    cnt--;
                }
            }
        }
    }
    else
    {
        for (auto &use : uses)
            if (use == old_op)
            {
                use->removeUse(this);
                use = new_op;
                new_op->addUse(this);
            }
    }
}

void SparseCondConstProp::pass()
{
    for (auto func = unit->begin(); func != unit->end(); func++)
    {
        pass(*func);
    }
}

void SparseCondConstProp::pass(Function *func)
{
    func->ComputeDom();

    status_map.clear();
    value_map.clear();
    marked.clear();
    cfg_worklist.clear();
    ssa_worklist.clear();
    eq_cond_worklist.clear();

    SimplifyCFG sc(unit);
    sc.pass(func);

    // initialize status_map & value_map
    for (auto &block : func->getBlockList())
        for (auto inst = block->begin(); inst != block->end(); inst = inst->getNext())
        {
            std::vector<Operand *> ops;
            if (!inst->isCond() && !inst->isRet() && !inst->isStore() && !inst->isUncond())
                ops.push_back(inst->getDef());
            ops.insert(ops.end(), inst->getUses().begin(), inst->getUses().end());
            for (auto &ope : ops)
            {
                if (!status_map.count(ope))
                {
                    if (ope->getType()->isPTR())
                    {
                        if (dynamic_cast<PointerType *>(ope->getType())->getValType()->isConst())
                            status_map[ope] = CONST;
                        else
                            status_map[ope] = NAC; // TODO : 副作用优化
                    }
                    else if (ope->getType()->isARRAY())
                    {
                        status_map[ope] = ope->getType()->isConst() ? CONST : NAC;
                    }
                    else if (ope->getEntry()->isTemporary())
                    {
                        if (ope->getType()->isConst())
                        {
                            status_map[ope] = CONST;
                            value_map[ope] = ope->getEntry()->getValue();
                        }
                        else
                            status_map[ope] = UNDEF;
                    }
                    else if (ope->getEntry()->isVariable())
                    {
                        if (ope->getType()->isConst())
                        {
                            status_map[ope] = CONST;
                            value_map[ope] = ope->getEntry()->getValue();
                        }
                        else
                            status_map[ope] = NAC;
                    }
                    else
                    {
                        assert(ope->getEntry()->isConstant() && ope->getType()->isConst());
                        status_map[ope] = CONST;
                        value_map[ope] = ope->getEntry()->getValue();
                    }
                }
            }
        }

    // printStat();

    // compute status & value
    cfg_worklist.push_back({nullptr, func->getEntry()});
    auto i = 0U;
    auto j = 0U;
    auto k = 0U;
    while (i < cfg_worklist.size() || j < ssa_worklist.size() || k < eq_cond_worklist.size())
    {
        // while (i < cfg_worklist.size() || j < ssa_worklist.size())
        // {
        while (i < cfg_worklist.size())
        {
            auto [pre_bb, bb] = cfg_worklist[i++];

            if (marked.count({pre_bb, bb}) != 0)
                continue;
            marked.insert({pre_bb, bb});

            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
                visit(inst);
        }

        while (j < ssa_worklist.size())
        {
            auto inst = ssa_worklist[j++];
            auto bb = inst->getParent();

            for (auto pre_bb_iter = bb->pred_begin(); pre_bb_iter != bb->pred_end(); pre_bb_iter++)
            {
                auto pre_bb = *pre_bb_iter;
                if (marked.count({pre_bb, bb}) != 0)
                {
                    visit(inst);
                    break;
                }
            }
        }
        // }
        while (k < eq_cond_worklist.size())
        {
            auto eq_cond_data = eq_cond_worklist[k++];
            auto inst = eq_cond_data.inst;
            auto op = eq_cond_data.left_op;
            auto val_op = eq_cond_data.right_op;
            auto pre_bb = eq_cond_data.pre_bb;

            if (status_map[val_op] != CONST)
                continue;

            double val = value_map[val_op];
            auto const_op = new Operand(new ConstantSymbolEntry(Var2Const(op->getType()), val));
            inst->replaceUsesWith(op, const_op, pre_bb);
            status_map[const_op] = CONST;
            value_map[const_op] = val;

            auto bb = inst->getParent();
            for (auto pre_bb_iter = bb->pred_begin(); pre_bb_iter != bb->pred_end(); pre_bb_iter++)
            {
                auto pre_bb = *pre_bb_iter;
                if (marked.count({pre_bb, bb}) != 0)
                {
                    visit(inst);
                    break;
                }
            }
        }
    }

    // const fold
    constFold(func);
}

void SparseCondConstProp::visit(Instruction *inst)
{
    int cur_status = -1;
    switch (inst->getInstType())
    {
    case Instruction::BINARY:
    {
        if (status_map[inst->getUses()[0]] == NAC || status_map[inst->getUses()[1]] == NAC)
            cur_status = NAC;
        else if (status_map[inst->getUses()[0]] == UNDEF || status_map[inst->getUses()[1]] == UNDEF)
            assert(status_map[inst->getDef()] == UNDEF || status_map[inst->getDef()] == NAC);
        else
        {
            assert(status_map[inst->getUses()[0]] == CONST && status_map[inst->getUses()[1]] == CONST);
            cur_status = CONST;
            switch (dynamic_cast<BinaryInstruction *>(inst)->getOpcode())
            {
            case BinaryInstruction::ADD:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] + value_map[inst->getUses()[1]];
                break;
            case BinaryInstruction::SUB:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] - value_map[inst->getUses()[1]];
                break;
            case BinaryInstruction::MUL:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] * value_map[inst->getUses()[1]];
                break;
            case BinaryInstruction::DIV:
            {
                if (inst->getDef()->getType()->isFloat())
                    value_map[inst->getDef()] = value_map[inst->getUses()[0]] / value_map[inst->getUses()[1]];
                else
                {
                    assert(inst->getDef()->getType()->isInt());
                    value_map[inst->getDef()] = (int)value_map[inst->getUses()[0]] / (int)value_map[inst->getUses()[1]];
                }
                break;
            }
            case BinaryInstruction::MOD:
                value_map[inst->getDef()] = (int)value_map[inst->getUses()[0]] % (int)value_map[inst->getUses()[1]];
                break;
            default:
                assert(0 && "unimplemented binary inst type");
            }
        }
        break;
    }
    case Instruction::COND:
    {
        if (status_map[inst->getUses()[0]] == UNDEF)
            break;
        auto true_bb = dynamic_cast<CondBrInstruction *>(inst)->getTrueBranch();
        auto false_bb = dynamic_cast<CondBrInstruction *>(inst)->getFalseBranch();
        if (status_map[inst->getUses()[0]] == CONST)
        {
            if (value_map[inst->getUses()[0]] == 0)
            {
                if (!marked.count({inst->getParent(), false_bb}))
                    cfg_worklist.push_back({inst->getParent(), false_bb});
            }
            else
            {
                assert(value_map[inst->getUses()[0]] == 1);
                if (!marked.count({inst->getParent(), true_bb}))
                    cfg_worklist.push_back({inst->getParent(), true_bb});
            }
        }
        else
        {
            assert(status_map[inst->getUses()[0]] == NAC);
            for (auto bb : {true_bb, false_bb})
                if (!marked.count({inst->getParent(), bb}))
                    cfg_worklist.push_back({inst->getParent(), bb});
        }
        break;
    }
    case Instruction::UNCOND:
    {
        auto dst_bb = dynamic_cast<UncondBrInstruction *>(inst)->getBranch();
        if (!marked.count({inst->getParent(), dst_bb}))
            cfg_worklist.push_back({inst->getParent(), dst_bb});
        break;
    }
    case Instruction::RET:
    {
        break;
    }
    case Instruction::LOAD:
    {
        if (inst->getUses()[0]->getEntry()->isVariable())
        {
            if (status_map[inst->getUses()[0]] == CONST)
            {
                cur_status = CONST;
                if (inst->getDef()->getType()->isFloat())
                    value_map[inst->getDef()] = (float)inst->getUses()[0]->getEntry()->getValue();
                else
                {
                    assert(inst->getDef()->getType()->isInt());
                    value_map[inst->getDef()] = (int)inst->getUses()[0]->getEntry()->getValue();
                }
            }
            else
            {
                assert(status_map[inst->getUses()[0]] == NAC);
                cur_status = NAC;
            }
        }
        else
        {
            assert(inst->getUses()[0]->getEntry()->isTemporary());
            if (status_map[inst->getDef()] == UNDEF)
                cur_status = NAC;
        }
        break;
    }
    case Instruction::STORE:
    {
        break;
    }
    case Instruction::CMP:
    {
        if (inst->getNext()->isCond())
        {
            auto true_bb = dynamic_cast<CondBrInstruction *>(inst->getNext())->getTrueBranch();
            auto false_bb = dynamic_cast<CondBrInstruction *>(inst->getNext())->getFalseBranch();
            if (status_map[inst->getUses()[0]] != CONST && status_map[inst->getUses()[1]] == CONST)
            {
                switch (dynamic_cast<CmpInstruction *>(inst)->getOpcode())
                {
                case CmpInstruction::E:
                {
                    if (true_bb->getNumOfPred() == 1)
                    {
                        for (auto userInst : inst->getUses()[0]->getUses())
                        {
                            if (userInst->getParent() == true_bb || userInst->getParent()->getSDoms().count(true_bb))
                                eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], nullptr});
                            else if (userInst->isPHI())
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[0])
                                    {
                                        if (pre_bb == true_bb || pre_bb->getSDoms().count(true_bb))
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], pre_bb});
                                    }
                            }
                        }
                    }
                    else
                    {
                        for (auto userInst : inst->getUses()[0]->getUses())
                        {
                            if (userInst->isPHI() && userInst->getParent() == true_bb)
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[0])
                                    {
                                        if (pre_bb == inst->getParent())
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], pre_bb});
                                    }
                            }
                        }
                    }
                    break;
                }
                case CmpInstruction::NE:
                {
                    if (false_bb->getNumOfPred() == 1)
                    {
                        for (auto userInst : inst->getUses()[0]->getUses())
                        {
                            if (userInst->getParent() == false_bb || userInst->getParent()->getSDoms().count(false_bb))
                                eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], nullptr});
                            else if (userInst->isPHI())
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[0])
                                    {
                                        if (pre_bb == false_bb || pre_bb->getSDoms().count(false_bb))
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], pre_bb});
                                    }
                            }
                        }
                    }
                    else
                    {
                        for (auto userInst : inst->getUses()[0]->getUses())
                        {
                            if (userInst->isPHI() && userInst->getParent() == false_bb)
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[0])
                                    {
                                        if (pre_bb == inst->getParent())
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[0], inst->getUses()[1], pre_bb});
                                    }
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }
            else if (status_map[inst->getUses()[1]] != CONST && status_map[inst->getUses()[0]] == CONST)
            {
                switch (dynamic_cast<CmpInstruction *>(inst)->getOpcode())
                {
                case CmpInstruction::E:
                {
                    if (true_bb->getNumOfPred() == 1)
                    {
                        for (auto userInst : inst->getUses()[1]->getUses())
                        {
                            if (userInst->getParent() == true_bb || userInst->getParent()->getSDoms().count(true_bb))
                                eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], nullptr});
                            else if (userInst->isPHI())
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[1])
                                    {
                                        if (pre_bb == true_bb || pre_bb->getSDoms().count(true_bb))
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], pre_bb});
                                    }
                            }
                        }
                    }
                    else
                    {
                        for (auto userInst : inst->getUses()[1]->getUses())
                        {
                            if (userInst->isPHI() && userInst->getParent() == true_bb)
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[1])
                                    {
                                        if (pre_bb == inst->getParent())
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], pre_bb});
                                    }
                            }
                        }
                    }
                    break;
                }
                case CmpInstruction::NE:
                {
                    if (false_bb->getNumOfPred() == 1)
                    {
                        for (auto userInst : inst->getUses()[1]->getUses())
                        {
                            if (userInst->getParent() == false_bb || userInst->getParent()->getSDoms().count(false_bb))
                                eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], nullptr});
                            else if (userInst->isPHI())
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[1])
                                    {
                                        if (pre_bb == false_bb || pre_bb->getSDoms().count(false_bb))
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], pre_bb});
                                    }
                            }
                        }
                    }
                    else
                    {
                        for (auto userInst : inst->getUses()[1]->getUses())
                        {
                            if (userInst->isPHI() && userInst->getParent() == false_bb)
                            {
                                for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(userInst)->getSrcs())
                                    if (src == inst->getUses()[1])
                                    {
                                        if (pre_bb == inst->getParent())
                                            eq_cond_worklist.push_back(EQ_COND_DATA{userInst, inst->getUses()[1], inst->getUses()[0], pre_bb});
                                    }
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }
        if (status_map[inst->getUses()[0]] == NAC || status_map[inst->getUses()[1]] == NAC)
        {
            cur_status = NAC;
        }
        else if (status_map[inst->getUses()[0]] == UNDEF || status_map[inst->getUses()[1]] == UNDEF)
            assert(status_map[inst->getDef()] == UNDEF || status_map[inst->getDef()] == NAC);
        else
        {
            assert(status_map[inst->getUses()[0]] == CONST && status_map[inst->getUses()[1]] == CONST);
            cur_status = CONST;
            switch (dynamic_cast<CmpInstruction *>(inst)->getOpcode())
            {
            case CmpInstruction::E:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] == value_map[inst->getUses()[1]];
                break;
            case CmpInstruction::NE:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] != value_map[inst->getUses()[1]];
                break;
            case CmpInstruction::L:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] < value_map[inst->getUses()[1]];
                break;
            case CmpInstruction::LE:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] <= value_map[inst->getUses()[1]];
                break;
            case CmpInstruction::G:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] > value_map[inst->getUses()[1]];
                break;
            case CmpInstruction::GE:
                value_map[inst->getDef()] = value_map[inst->getUses()[0]] >= value_map[inst->getUses()[1]];
                break;
            default:
                assert(0 && "unimplemented cmp inst type");
            }
        }
        break;
    }
    case Instruction::ALLOCA:
    {
        break;
    }
    case Instruction::ZEXT:
    {
        if (status_map[inst->getUses()[0]] == NAC)
            cur_status = NAC;
        else if (status_map[inst->getUses()[0]] == UNDEF)
            assert(status_map[inst->getDef()] == UNDEF || status_map[inst->getDef()] == NAC);
        else
        {
            assert(status_map[inst->getUses()[0]] == CONST);
            cur_status = CONST;
            value_map[inst->getDef()] = value_map[inst->getUses()[0]];
        }
        break;
    }
    case Instruction::IFCAST:
    {
        if (status_map[inst->getUses()[0]] == NAC)
            cur_status = NAC;
        else if (status_map[inst->getUses()[0]] == UNDEF)
            assert(status_map[inst->getDef()] == UNDEF || status_map[inst->getDef()] == NAC);
        else
        {
            assert(status_map[inst->getUses()[0]] == CONST);
            cur_status = CONST;
            switch (dynamic_cast<IntFloatCastInstruction *>(inst)->getOpcode())
            {
            case IntFloatCastInstruction::S2F:
                value_map[inst->getDef()] = (float)value_map[inst->getUses()[0]];
                break;
            case IntFloatCastInstruction::F2S:
                value_map[inst->getDef()] = (int)value_map[inst->getUses()[0]];
                break;
            default:
                assert(0 && "unimplemented IF cast inst");
            }
        }
        break;
    }
    case Instruction::CALL:
    {
        for (auto use : inst->getUses())
            if (status_map[use] == UNDEF)
            {
                assert(status_map[inst->getDef()] == UNDEF || status_map[inst->getDef()] == NAC);
                cur_status = status_map[inst->getDef()];
                break;
            }
        if (cur_status == -1)
            cur_status = NAC;
        break;
    }
    case Instruction::PHI:
    {
        Operand *const_src = nullptr;
        for (auto [pre_bb, src] : dynamic_cast<PhiInstruction *>(inst)->getSrcs())
        {
            if (status_map[src] == CONST && const_src == nullptr)
                const_src = src;
            else if (!marked.count({pre_bb, inst->getParent()}))
                continue;
            if (status_map[src] == UNDEF)
            {
                cur_status = status_map[inst->getDef()];
                break;
            }
            if (status_map[src] == NAC || (status_map[src] == CONST && const_src != nullptr && value_map[src] != value_map[const_src]))
            {
                cur_status = NAC;
                break;
            }
        }
        if (cur_status == -1)
        {
            assert(const_src != nullptr);
            value_map[inst->getDef()] = value_map[const_src];
            cur_status = CONST;
        }
        break;
    }
    case Instruction::GEP:
    {
        // TODO ：参数数组寻址
        if ((dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType())->isARRAY())
        {
            IdentifierSymbolEntry *arr = nullptr;
            if (inst->getUses()[0]->getEntry()->isTemporary())
            {
                assert(inst->getUses()[0]->getDef());
                if (!inst->getUses()[0]->getDef()->isAlloca())
                {
                    assert(inst->getUses()[0]->getDef()->isLoad() || inst->getUses()[0]->getDef()->isGep());
                    break;
                }
                arr = dynamic_cast<IdentifierSymbolEntry *>(dynamic_cast<AllocaInstruction *>(inst->getUses()[0]->getDef())->getSymPtr());
            }
            else
            {
                assert(inst->getUses()[0]->getEntry()->isVariable());
                arr = dynamic_cast<IdentifierSymbolEntry *>(inst->getUses()[0]->getEntry());
            }
            bool const_ptr = arr->getType()->isConst();
            auto offset = 0LL;
            auto arrVals = arr->getArrVals();
            auto arrType = dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType();
            auto cur_size = arrType->getSize() / dynamic_cast<ArrayType *>(arrType)->getElemType()->getSize();
            auto dims = ((ArrayType *)(dynamic_cast<PointerType *>(inst->getUses()[0]->getType())->getValType()))->fetch();
            auto k = 0U;
            // while (const_ptr && inst->isGep())
            // {
            for (auto i = 1U; i != inst->getUses().size(); i++)
            {
                if (status_map[inst->getUses()[i]] == NAC || status_map[inst->getUses()[i]] == UNDEF)
                {
                    const_ptr = false;
                    break;
                }
                offset += value_map[inst->getUses()[i]] * cur_size;
                if (k != dims.size())
                    cur_size /= dims[k++];
            }
            inst = inst->getNext();
            // }
            if (const_ptr)
            {
                if (inst->isStore())
                    break;
                if (inst->isLoad())
                {
                    cur_status = CONST;
                    value_map[inst->getDef()] = arrVals[offset];
                }
            }
        }
        break;
    }
    default:
        assert(0 && "unimplemented inst type");
    }

    if (cur_status != -1 && cur_status != status_map[inst->getDef()])
    {
        assert(!(status_map[inst->getDef()] == NAC && cur_status == UNDEF));
        status_map[inst->getDef()] = cur_status;
        if (cur_status == NAC)
            value_map.erase(inst->getDef());
        for (auto userInst : inst->getDef()->getUses())
            ssa_worklist.push_back(userInst);
    }
}

void SparseCondConstProp::constFold(Function *func)
{
    std::vector<Instruction *> freeInsts;
    for (auto &block : func->getBlockList())
        for (auto inst = block->begin(); inst != block->end(); inst = inst->getNext())
        {
            if (!inst->isCond() && !inst->isRet() && !inst->isStore() && !inst->isUncond() && status_map[inst->getDef()] == CONST && !inst->getDef()->getType()->isPTR())
            {
                inst->replaceAllUsesWith(new Operand(new ConstantSymbolEntry(Var2Const(inst->getDef()->getType()), value_map[inst->getDef()])));
                inst->getParent()->remove(inst);
                freeInsts.push_back(inst);
            }
        }

    // cond_br with const cond
    for (auto bb : func->getBlockList())
    {
        if (bb->rbegin()->isCond())
        {
            auto cond_br = dynamic_cast<CondBrInstruction *>(bb->rbegin());
            if (cond_br->constEval())
            {
                freeInsts.push_back(cond_br);
            }
        }
    }

    // 删除def not use
    for (auto &block : func->getBlockList())
        for (auto inst = block->begin(); inst != block->end(); inst = inst->getNext())
        {
            if (!inst->isCritical() && !inst->isCond() && !inst->isRet() && !inst->isStore() && !inst->isUncond() && inst->getDef()->usersNum() == 0)
            {
                inst->getParent()->remove(inst);
                freeInsts.push_back(inst);
            }
        }

    for (auto inst : freeInsts)
    {
        delete inst;
    }
    freeInsts.clear();

    SimplifyCFG sc(unit);
    sc.pass(func);
}

void SparseCondConstProp::printStat()
{
    fprintf(stderr, "-----------------------------------------------\n");
    for (auto kv : status_map)
    {
        fprintf(stderr, "status_map[%s] = %s\n", kv.first->toStr().c_str(), (kv.second == 0) ? "UNDEF" : (kv.second == 1) ? "CONST"
                                                                                                                          : "NAC");
    }
    for (auto kv : value_map)
    {
        fprintf(stderr, "value_map[%s] = %lf\n", kv.first->toStr().c_str(), kv.second);
    }
    fprintf(stderr, "-----------------------------------------------\n");
}
