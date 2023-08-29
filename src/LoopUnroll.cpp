#include "LoopUnroll.h"

static Operand *copyOperand(Operand *ope)
{
    if (ope->getEntry()->getType()->isConst())
        return new Operand(new ConstantSymbolEntry(ope->getType(), ope->getEntry()->getValue()));
    else
    {
        assert(ope->getEntry()->isTemporary());
        return new Operand(new TemporarySymbolEntry(ope->getType(), SymbolTable::getLabel()));
    }
}

void LoopUnroll::pass()
{
    while (true)
    {
        auto cand = FindCandidateLoop();
        if (cand == nullptr)
            break;
        fprintf(stderr, "loop be unrolled\n");
        Unroll(cand);
        fprintf(stderr, "loop unrolled finish\n");
    }
}

/*
    Find Candidate Loop, don't consider instrs that have FuncCall temporarily;
*/
Loop *LoopUnroll::FindCandidateLoop()
{
    for (auto f : unit->getFuncList())
    {
        auto analyzer = LoopAnalyzer(f);
        analyzer.analyze();
        for (auto loop : analyzer.getLoops())
        {
            if (loop->subLoops.size() != 0)
                continue;
            if (loop->header_bb != loop->exiting_bb) // TODO
                continue;
            if (analyzer.multiOr(loop))
                continue;
            // 排除break/while(a and b)
            if (analyzer.hasLeak(loop))
                continue;
            assert(loop->exiting_bb->rbegin()->isCond());
            assert(loop->exiting_bb->rbegin()->getUses()[0]->getDef() && loop->exiting_bb->rbegin()->getUses()[0]->getDef()->isCmp());
            auto cmp = loop->exiting_bb->rbegin()->getUses()[0]->getDef();

            InductionVar *ind_var = nullptr;
            for (auto ind : loop->inductionVars)
            {
                auto du_chain = ind->du_chains[0];
                if (cmp->getUses()[0] == du_chain[1]->getDef() || cmp->getUses()[1] == du_chain[1]->getDef())
                {
                    ind_var = ind;
                    break;
                }
            }
            if (ind_var == nullptr)
                continue;
            if (ind_var->du_chains.size() != 1)
            {
                continue;
            }
            auto du_chain = ind_var->du_chains[0];
            if (du_chain.size() != 2)
            {
                continue;
            }
            assert(du_chain[0]->isPHI());
            if (!du_chain[1]->isBinary())
            {
                continue;
            }
            if (du_chain[1]->getInstType() != BinaryInstruction::ADD && du_chain[1]->getInstType() != BinaryInstruction::MUL)
                continue;

            auto range_second = cmp->getUses()[0] == du_chain[1]->getDef() ? cmp->getUses()[1] : cmp->getUses()[0];
            if (!range_second->getType()->isConst() && range_second->getDef() && loop->loop_bbs.count(range_second->getDef()->getParent()))
                continue;
            auto stride = du_chain[1]->getUses()[0] == du_chain[0]->getDef() ? du_chain[1]->getUses()[1] : du_chain[1]->getUses()[0];
            if (!stride->getType()->isConst() && stride->getDef() && loop->loop_bbs.count(stride->getDef()->getParent()))
                continue;
            auto phi = du_chain[0];
            auto phi_srcs = dynamic_cast<PhiInstruction *>(phi)->getSrcs();
            assert(phi_srcs.size() == 2);
            assert(phi_srcs.count(loop->exiting_bb));
            auto range_first = phi->getUses()[0] == phi_srcs[loop->exiting_bb] ? phi->getUses()[1] : phi->getUses()[0];
            if (!range_first->getType()->isConst() && range_first->getDef() && loop->loop_bbs.count(range_first->getDef()->getParent()))
                continue;
            if (range_first->getType()->isFloat() || range_second->getType()->isFloat())
                continue;
            if (loop->header_bb->Unrolled())
                continue;
            else
                loop->header_bb->SetUnrolled();
            return loop;
        }
    }
    return nullptr;
}

void LoopUnroll::specialCopyInstructions(BasicBlock *bb, int num, Operand *endOp, Operand *strideOp, bool ifall)
{
    /*
     *                   ----
     *                   ↓  ↑
     * Special:  pred -> body -> Exitbb      ==>     pred -> newbody -> Exitbb
     *
     *
     */

    std::vector<Instruction *> freeInsts;
    std::map<Instruction *, Operand *> to_replace;
    std::vector<Instruction *> prevInsts, nextInsts;
    std::map<Operand *, Operand *> outer_bridge, reverse_outer_bridge;
    for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        nextInsts.push_back(inst);
    if (nextInsts.size() > MAXUNROLLNUM)
        return;
    for (int k = 0; k < num - 1; k++)
    {
        freeInsts.push_back(bb->rbegin());

        prevInsts.clear();
        for (auto inst : nextInsts)
        {
            prevInsts.push_back(inst);
        }
        nextInsts.clear();
        for (auto inst : prevInsts)
        {
            auto copyInst = inst->copy();
            for (auto use : copyInst->getUses())
                use->addUse(copyInst);
            nextInsts.push_back(copyInst);
        }
        std::map<Operand *, Operand *> bridge;
        for (auto inst : nextInsts)
        {
            if (!inst->hasNoDef())
            {
                auto new_def = copyOperand(inst->getDef());
                bridge[inst->getDef()] = new_def;
                Operand *from = inst->getDef(), *to = new_def;
                if (reverse_outer_bridge.count(inst->getDef()))
                    from = reverse_outer_bridge[inst->getDef()];
                outer_bridge[from] = to;
                reverse_outer_bridge[to] = from;
                inst->setDef(new_def);
            }
        }

        size_t PhiInstructionCnt = 0;
        for (; PhiInstructionCnt < nextInsts.size(); PhiInstructionCnt++)
        {
            if (!nextInsts[PhiInstructionCnt]->isPHI())
                break;
        }
        for (size_t i = PhiInstructionCnt; i < nextInsts.size(); i++)
        {
            auto inst = nextInsts[i];
            for (auto use : inst->getUses())
            {
                if (bridge.count(use))
                {
                    inst->replaceUsesWith(use, bridge[use]);
                }
            }
        }

        for (auto inst : nextInsts)
        {
            bb->insertBack(inst);
        }

        // bb->output();

        for (size_t i = 0; i < PhiInstructionCnt; i++)
        {
            auto phi = dynamic_cast<PhiInstruction *>(nextInsts[i]);
            assert(phi->getSrcs().size() == 2 && phi->getSrcs().count(bb));
            to_replace[phi] = phi->getSrcs()[bb];
            freeInsts.push_back(phi);
        }

        for (size_t i = 0; i < PhiInstructionCnt; i++)
        {
            auto inst = nextInsts[i];
            for (auto use : inst->getUses())
            {
                if (bridge.count(use))
                {
                    inst->replaceUsesWith(use, bridge[use]);
                }
            }
        }
    }
    for (auto [phi, op] : to_replace)
        phi->replaceAllUsesWith(op);

    for (auto [old_op, new_op] : outer_bridge)
    {
        for (auto user : old_op->getUses())
        {
            if (user->getParent() != bb)
                user->replaceUsesWith(old_op, new_op);
        }
    }

    if (ifall)
    {
        bb->removePred(bb);
        bb->removeSucc(bb);

        for (auto inst = bb->begin(); inst != bb->end() && inst->isPHI(); inst = inst->getNext())
        {
            auto phi = dynamic_cast<PhiInstruction *>(inst);
            assert(bb->getNumOfPred() == 1);
            phi->replaceAllUsesWith(phi->getSrcs()[*bb->pred_begin()]);
            freeInsts.push_back(phi);
        }
        freeInsts.push_back(bb->rbegin());
        assert(bb->getNumOfSucc() == 1);
        new UncondBrInstruction((*bb->succ_begin()), bb);
    }
    for (auto inst : freeInsts)
        delete inst;

    std::cout << "SpecialUnroll Finished!\n";
}

// 只考虑+1的情况先 不管浮点？
void LoopUnroll::normalCopyInstructions(BasicBlock *condbb, BasicBlock *bodybb, Operand *beginOp, Operand *endOp, Operand *strideOp)
{
    /*
     *                     --------                                    ------------        -----------
     *                     ↓      ↑                                    ↓          ↑        ↓         ↑
     * Normal:  pred ->  cond -> body  Exitbb      ==>     pred -> rescond -> resbody maincond -> mainbody Exitbb
     *                     ↓             ↑                             ↓                  ↑   ↓              ↑
     *                     ---------------                             --------------------   ----------------
     *
     */
    BasicBlock *newCondBB = new BasicBlock(condbb->getParent());
    BasicBlock *resBodyBB = new BasicBlock(condbb->getParent());
    BasicBlock *resOutCond = new BasicBlock(condbb->getParent());

    BasicBlock *resoutCondSucc = nullptr;
    for (auto succBB = bodybb->succ_begin(); succBB != bodybb->succ_end(); succBB++)
    {
        if (*succBB != bodybb)
            resoutCondSucc = *succBB;
    }
    if (resoutCondSucc == nullptr)
    {
        return;
    }

    std::vector<Instruction *> InstList;
    CmpInstruction *cmp = nullptr;
    for (auto bodyinstr = bodybb->begin(); bodyinstr != bodybb->end(); bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isCmp())
        {
            cmp = (CmpInstruction *)bodyinstr;
            break;
        }
        InstList.push_back(bodyinstr);
    }

    bool ifPlusOne = false;
    BinaryInstruction *binPlusOne;
    if (cmp->getOpcode() == CmpInstruction::LE || cmp->getOpcode() == CmpInstruction::GE)
    {
        ifPlusOne = true;
    }
    Operand *countDef = new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel()));
    BinaryInstruction *calCount = new BinaryInstruction(BinaryInstruction::SUB, countDef, endOp, beginOp, nullptr);
    if (ifPlusOne)
    {
        binPlusOne = new BinaryInstruction(BinaryInstruction::ADD, new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())), countDef, new Operand(new ConstantSymbolEntry(TypeSystem::intType, 1)), nullptr);
        countDef = binPlusOne->getDef();
    }
    BinaryInstruction *binMod = new BinaryInstruction(BinaryInstruction::MOD, new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())), countDef, new Operand(new ConstantSymbolEntry(TypeSystem::intType, UNROLLNUM)), nullptr);
    CmpInstruction *cmpEZero = new CmpInstruction(CmpInstruction::E, new Operand(new TemporarySymbolEntry(TypeSystem::boolType, SymbolTable::getLabel())), binMod->getDef(), new Operand(new ConstantSymbolEntry(TypeSystem::intType, 0)), nullptr);
    CondBrInstruction *condBr = new CondBrInstruction(bodybb, resBodyBB, cmpEZero->getDef(), nullptr);

    condbb->addSucc(newCondBB);
    condbb->removeSucc(bodybb);
    CondBrInstruction *condbbBr = (CondBrInstruction *)condbb->rbegin();
    if (condbbBr->getTrueBranch() == bodybb)
    {
        condbbBr->setTrueBranch(newCondBB);
    }
    else if (condbbBr->getFalseBranch() == bodybb)
    {
        condbbBr->setFalseBranch(newCondBB);
    }
    else
    {
        std::cout << "conbb succ not right" << std::endl;
    }

    newCondBB->addPred(condbb);
    newCondBB->addSucc(bodybb);
    newCondBB->addSucc(resBodyBB);
    newCondBB->insertBack(calCount);
    // 得看是否有equal
    if (ifPlusOne)
    {
        newCondBB->insertBack(binPlusOne);
    }
    newCondBB->insertBack(binMod);
    newCondBB->insertBack(cmpEZero);
    newCondBB->insertBack(condBr);
    // newcond 对的

    //
    resBodyBB->addPred(newCondBB);
    resBodyBB->addPred(resBodyBB);
    resBodyBB->addSucc(resBodyBB);
    resBodyBB->addSucc(resOutCond);
    // resBody第一条得是phi指令
    // 末尾添加cmp和br指令
    std::vector<Instruction *> resBodyInstList;
    for (auto ins : InstList)
    {
        auto newIns = ins->copy();
        resBodyInstList.push_back(newIns);
    }
    std::map<Operand *, Operand *> resBodyReplaceMap;
    for (auto resIns : resBodyInstList)
    {
        if (!resIns->hasNoDef())
        {
            if (resIns->isPHI())
            {
                PhiInstruction *phi = (PhiInstruction *)resIns;
                Operand *condOp = phi->getSrcs()[condbb];
                Operand *bodyOp = phi->getSrcs()[bodybb];
                phi->removeEdge(condbb);
                phi->removeEdge(bodybb);
                phi->addEdge(newCondBB, condOp);
                phi->addEdge(resBodyBB, bodyOp);
            }
            Operand *oldDef = resIns->getDef();
            Operand *newDef = new Operand(new TemporarySymbolEntry(oldDef->getType(), SymbolTable::getLabel()));
            resBodyReplaceMap[oldDef] = newDef;
            resIns->setDef(newDef);
        }
    }

    Operand *resNumPhiDef = new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel()));
    Operand *binIncDef = new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel()));
    PhiInstruction *resNumPhi = new PhiInstruction(resNumPhiDef, false, nullptr);
    resNumPhi->addEdge(newCondBB, new Operand(new ConstantSymbolEntry(TypeSystem::intType, 0)));
    resNumPhi->addEdge(resBodyBB, binIncDef);
    BinaryInstruction *binInc = new BinaryInstruction(BinaryInstruction::ADD, binIncDef, resNumPhi->getDef(), new Operand(new ConstantSymbolEntry(TypeSystem::intType, 1)), nullptr);
    CmpInstruction *resBodyCmp = new CmpInstruction(CmpInstruction::NE, new Operand(new TemporarySymbolEntry(TypeSystem::boolType, SymbolTable::getLabel())), binIncDef, binMod->getDef(), nullptr);
    CondBrInstruction *resBodyBr = new CondBrInstruction(resBodyBB, resOutCond, resBodyCmp->getDef(), nullptr);

    for (auto resIns : resBodyInstList)
    {
        for (auto useOp : resIns->getUses())
        {
            if (resBodyReplaceMap.find(useOp) != resBodyReplaceMap.end())
            {
                resIns->replaceUsesWith(useOp, resBodyReplaceMap[useOp]);
            }
            else
            {
                useOp->addUse(resIns);
            }
        }
    }

    resBodyBB->insertBack(resBodyBr);
    resBodyBB->insertBefore(resBodyCmp, resBodyBr);
    resBodyBB->insertBefore(binInc, resBodyCmp);
    resBodyBB->insertFront(resNumPhi);
    for (auto resIns : resBodyInstList)
    {
        resBodyBB->insertBefore(resIns, binInc);
    }

    // resoutcond

    resOutCond->addPred(resBodyBB);
    resOutCond->addSucc(resoutCondSucc);
    resOutCond->addSucc(bodybb);
    resoutCondSucc->addPred(resOutCond);

    CmpInstruction *resOutCondCmp = (CmpInstruction *)cmp->copy();
    resOutCondCmp->setDef(new Operand(new TemporarySymbolEntry(TypeSystem::boolType, SymbolTable::getLabel())));
    resOutCondCmp->replaceUsesWith(strideOp, resBodyReplaceMap[strideOp]);
    CondBrInstruction *resOutCondBr = new CondBrInstruction(bodybb, resoutCondSucc, resOutCondCmp->getDef(), nullptr);
    resOutCond->insertBack(resOutCondBr);
    resOutCond->insertBefore(resOutCondCmp, resOutCondBr);

    bodybb->removePred(condbb);
    bodybb->addPred(newCondBB);
    bodybb->addPred(resOutCond);

    for (int i = 0; i < InstList.size(); i++)
    {
        if (InstList[i]->isPHI())
        {
            PhiInstruction *phi = (PhiInstruction *)InstList[i];
            Operand *condOp = phi->getSrcs()[condbb];
            Operand *bodyOp = phi->getSrcs()[bodybb];
            phi->removeEdge(condbb);
            phi->addEdge(newCondBB, condOp);
            phi->addEdge(resOutCond, resBodyReplaceMap[bodyOp]);
        }
    }

    specialCopyInstructions(bodybb, UNROLLNUM, endOp, strideOp, false);

    // 更改resoutSucc
    for (auto bodyinstr = resoutCondSucc->begin(); bodyinstr != resoutCondSucc->end(); bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isPHI())
        {
            PhiInstruction *phi = (PhiInstruction *)bodyinstr;
            Operand *originalOperand = phi->getSrcs()[bodybb];
            phi->addEdge(resOutCond, resBodyReplaceMap[originalOperand]);
        }
    }
}

void LoopUnroll::Unroll(Loop *loop)
{
    CmpInstruction *LoopCmp = (CmpInstruction *)(loop->exiting_bb->rbegin()->getUses()[0]->getDef());
    auto InductionV = loop->inductionVars;
    InductionVar *ind_var = nullptr;
    for (auto ind : loop->inductionVars)
    {
        auto du_chain = ind->du_chains[0];
        if (LoopCmp->getUses()[0] == du_chain[1]->getDef() || LoopCmp->getUses()[1] == du_chain[1]->getDef())
        {
            ind_var = ind;
            break;
        }
    }
    assert(ind_var != nullptr);
    auto Instr_path = ind_var->du_chains[0];
    std::set<Instruction *> Instr_Set;
    for (auto ip : Instr_path)
        Instr_Set.insert(ip);
    int begin = -1, end = -1, stride = -1;
    bool IsBeginCons, IsEndCons, IsStrideCons;
    IsBeginCons = IsEndCons = IsStrideCons = false;
    Operand *endOp = nullptr, *beginOp = nullptr, *strideOp = nullptr;
    BasicBlock *cond = loop->exiting_bb, *body = loop->header_bb;
    CmpInstruction *condCmp = (CmpInstruction *)cond->rbegin()->getUses()[0]->getDef();
    assert(condCmp->isCmp());
    int cmpCode = condCmp->getOpcode();
    if (condCmp == nullptr || cmpCode == CmpInstruction::E || cmpCode == CmpInstruction::NE)
    {
        std::cout << "condCmp is null or cmpType is not match" << std::endl;
        return;
    }
    auto uses_cmp = condCmp->getUses();
    Operand *Useop1 = uses_cmp[0], *Useop2 = uses_cmp[1];
    auto Add_Instr1 = Useop1->getDef(), Add_Instr2 = Useop2->getDef();
    bool needReverse = false;
    assert(Useop1 != nullptr && Useop2 != nullptr);
    if (Instr_Set.count(Add_Instr1))
    {
        strideOp = Useop1;
        endOp = Useop2;
        if (cmpCode == CmpInstruction::G || cmpCode == CmpInstruction::GE)
            needReverse = true;
    }
    else if (Instr_Set.count(Add_Instr2))
    {
        strideOp = Useop2;
        endOp = Useop1;
        if (cmpCode == CmpInstruction::L || cmpCode == CmpInstruction::LE)
            needReverse = true;
    }

    assert(strideOp != nullptr);
    PhiInstruction *head_phi = (PhiInstruction *)ind_var->du_chains[0][0];
    if (head_phi->getSrcs().size() > 2)
    {
        return;
    }
    for (auto srcs : head_phi->getSrcs())
        if (srcs.first != loop->exiting_bb)
            beginOp = srcs.second;

    if (needReverse)
        std::swap(beginOp, endOp);
    assert(beginOp != nullptr);
    std::cout << "BeginOp is " << beginOp->toStr() << " , EndOp is " << endOp->toStr() << "\n";
    Operand *step = nullptr;
    Instruction *binary_instr = ind_var->du_chains[0][1];
    if (!binary_instr->isBinary())
    {
        std::cout << "The Induction Change is not east to process";
        return;
    }
    int ivOpcode = binary_instr->getOpcode();
    if (ivOpcode == BinaryInstruction::SUB || ivOpcode == BinaryInstruction::DIV || ivOpcode == BinaryInstruction::MOD)
    {
        std::cout << "BinaryInstruction Type is SUB, DIV or MOD, do nothing" << std::endl;
        return;
    }
    for (auto useOp : binary_instr->getUses())
    {
        if (useOp->getDef() == nullptr || useOp->getType()->isConst())
            step = useOp;
        else if (!loop->loop_bbs.count(useOp->getDef()->getParent()))
            step = useOp;
    }
    if (step == nullptr)
    {
        std::cout << "can't get step" << std::endl;
        return;
    }

    if (beginOp->getType()->isConst())
    {
        IsBeginCons = true;
        begin = beginOp->getEntry()->getValue();
    }

    if (endOp->getType()->isConst())
    {
        IsEndCons = true;
        end = endOp->getEntry()->getValue();
    }

    if (step->getType()->isConst())
    {
        IsStrideCons = true;
        stride = step->getEntry()->getValue();
    }

    if (IsBeginCons && IsEndCons && IsStrideCons)
    {
        if (ivOpcode == BinaryInstruction::ADD)
        {
            int count = 0;
            switch (cmpCode)
            {
            case CmpInstruction::LE:
            case CmpInstruction::GE:
                for (int i = begin; i <= end; i += stride)
                    count++;
                break;
            case CmpInstruction::L:
            case CmpInstruction::G:
                for (int i = begin; i < end; i += stride)
                    count++;
                break;
            default:
                std::cout << "Wrong CmpInstrucion Type!\n";
                break;
            }
            if (count < MAXUNROLLNUM)
                specialCopyInstructions(body, count, endOp, strideOp, true);
            else
            {
                std::cout << "Too much InstrNum, don't unroll\n";
                return;
            }
        }
        else if (ivOpcode == BinaryInstruction::MUL)
        {
            int count = 0;
            switch (cmpCode)
            {
            case CmpInstruction::LE:
            case CmpInstruction::GE:
                for (int i = begin; i <= end; i *= stride)
                    count++;
                break;
            case CmpInstruction::L:
            case CmpInstruction::G:
                for (int i = begin; i < end; i *= stride)
                    count++;
                break;
            default:
                std::cout << "Wrong CmpInstrucion Type!\n";
                break;
            }
            if (count < MAXUNROLLNUM)
                specialCopyInstructions(body, count, endOp, strideOp, true);
            else
            {
                std::cout << "Too much InstrNum, don't unroll\n";
                return;
            }
        }
        else
        {
            std::cout << "Binary OpType Unknown!\n";
            return;
        }
    }
    else if (IsStrideCons)
    {
        if (ivOpcode == BinaryInstruction::ADD && stride == 1)
        {
            // normalCopyInstructions(cond, body, beginOp, endOp, strideOp);
        }
    }
}
