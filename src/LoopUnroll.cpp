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
    while (true) {
        auto cand = FindCandidateLoop();
        if (cand == nullptr) break;
        fprintf(stderr, "loop be unrolled\n");
        Unroll(cand);
        fprintf(stderr, "loop unrolled finish\n");
    }
}

/*
    Find Candidate Loop, don't consider instrs that have FuncCall temporarily;
*/
Loop * LoopUnroll::FindCandidateLoop()
{
    for (auto f : unit->getFuncList())
    {
        auto analyzer = LoopAnalyzer(f);
        analyzer.analyze();
        for (auto loop : analyzer.getLoops())
        {
            if (loop->subLoops.size() != 0) continue;
            if (loop->header_bb != loop->exiting_bb) continue;
            LoopInfo loopinfo;
            loopinfo.loop_blocks = loop->loop_bbs;
            loopinfo.loop_header = loop->header_bb;
            loopinfo.loop_exiting_block = loop->exiting_bb;

            if (analyzer.multiOr(loop))
                continue;

            // 排除break/while(a and b)
            if (analyzer.hasLeak(loop))
                continue;

            // 判断归纳变量是否唯一、变化形式并记录
            bool flag = true;
            // for (auto ind : loop->inductionVars)
            // {
            //     if (ind->du_chains.size() != 1)
            //     {
            //         flag = false;
            //         break;
            //     }
            //     auto du_chain = ind->du_chains[0];
            //     if (du_chain.size() != 2)
            //     {
            //         flag = false;
            //         break;
            //     }
            //     assert(du_chain[0]->isPHI());
            //     if (!du_chain[1]->isBinary())
            //     {
            //         flag = false;
            //         break;
            //     }
            //     for (auto inst : du_chain)
            //     {
            //         for (auto use : inst->getDef()->getUses())
            //         {
            //             if (!loop->loop_bbs.count(use->getParent()))
            //             {
            //                 flag = false;
            //                 break;
            //             }
            //         }
            //     }
            // }
            if (!flag)
                continue;

            // if (loop->inductionVars.size() != analyzer.getLoopCnt(loop))
            //     continue;

            assert(loopinfo.loop_exiting_block->rbegin()->isCond());
            assert(loopinfo.loop_exiting_block->rbegin()->getUses()[0]->getDef() && loopinfo.loop_exiting_block->rbegin()->getUses()[0]->getDef()->isCmp());
            loopinfo.cmp = loopinfo.loop_exiting_block->rbegin()->getUses()[0]->getDef();

            InductionVar *ind_var = nullptr;
            for (auto ind : loop->inductionVars)
            {
                auto du_chain = ind->du_chains[0];
                if (loopinfo.cmp->getUses()[0] == du_chain[1]->getDef() || loopinfo.cmp->getUses()[1] == du_chain[1]->getDef())
                {
                    ind_var = ind;
                    break;
                }
            }
            if (ind_var == nullptr) continue;
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

            auto range_second = loopinfo.cmp->getUses()[0] == du_chain[1]->getDef() ? loopinfo.cmp->getUses()[1] : loopinfo.cmp->getUses()[0];
            if (!range_second->getType()->isConst() && range_second->getDef() && loop->loop_bbs.count(range_second->getDef()->getParent()))
                continue;
            assert(du_chain[0]->isPHI());
            assert(du_chain[1]->isBinary());
            auto stride = du_chain[1]->getUses()[0] == du_chain[0]->getDef() ? du_chain[1]->getUses()[1] : du_chain[1]->getUses()[0];
            if (!stride->getType()->isConst() && stride->getDef() && loop->loop_bbs.count(stride->getDef()->getParent()))
                continue;
            loopinfo.phi = du_chain[0];
            auto phi_srcs = dynamic_cast<PhiInstruction *>(loopinfo.phi)->getSrcs();
            assert(phi_srcs.size() == 2);
            assert(phi_srcs.count(loopinfo.loop_exiting_block));
            auto range_first = loopinfo.phi->getUses()[0] == phi_srcs[loopinfo.loop_exiting_block] ? loopinfo.phi->getUses()[1] : loopinfo.phi->getUses()[0];
            if (!range_first->getType()->isConst() && range_first->getDef() && loop->loop_bbs.count(range_first->getDef()->getParent()))
                continue;
            loopinfo.indvar_range = std::make_pair(range_first, range_second);
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
     *                            --------
     *                            ↓      ↑
     * Special:  pred -> cond -> body -> cond -> Exitbb      ==>     pred -> newbody -> Exitbb
     *                     ↓                       ↑
     *                     -------------------------
    */
    std::vector<Instruction *> preInstructionList;
    std::vector<Instruction *> nextInstructionList;
    std::vector<Instruction *> phis;
    CmpInstruction *cmp = (CmpInstruction*)bb->rbegin()->getPrev();
    std::vector<Operand *> finalOperands;
    std::map<Operand *, Operand *> begin_final_Map;
    // Find all phis and insert instrs in preInstructionList
    assert(cmp->isCmp());
    for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
    {
        if (instr->isPHI())
            phis.push_back(instr);
        if (instr->isCond() || instr->isUncond()) 
            continue;
        preInstructionList.push_back(instr);
    }

    // Insert next_instr into nextInstructionList
    for (auto preIns : preInstructionList)
    {
        Instruction *ins = preIns->copy();
        if (!preIns->isStore())
        {
            Operand *newDef = copyOperand(preIns->getDef());
            newDef->setDef(preIns->getDef()->getDef());
            begin_final_Map[preIns->getDef()] = newDef;
            finalOperands.push_back(preIns->getDef());
            ins->setDef(newDef);
            preIns->setDef(newDef); // PROBLEM1
        }
        nextInstructionList.push_back(ins);
    }

    for (int i = 0; i < preInstructionList.size(); i++)
    {
        Instruction *preIns = preInstructionList[i];
        for (auto useOp : preIns->getUses())
            if (begin_final_Map.find(useOp) != begin_final_Map.end())
                preIns->replaceUsesWith(useOp, begin_final_Map[useOp]); // PROBLEM2
    }

    for (auto nextIns : nextInstructionList)
    {
        for (auto useOp : nextIns->getUses())
        {
            if (begin_final_Map.find(useOp) != begin_final_Map.end())
                nextIns->replaceUsesWith(useOp, begin_final_Map[useOp]);
            else
                useOp->addUse(nextIns);
        }
        // nextInstructionList.push_back(preIns->copy());
    }

    std::map<Operand *, Operand *> replaceMap;
    for (int k = 0; k < num - 1; k++)
    {
        std::vector<Operand *> notReplaceOp;
        int calculatePhi = 0;
        for (int i = 0; i < nextInstructionList.size(); i++)
        {
            Instruction *preIns = preInstructionList[i];
            Instruction *nextIns = nextInstructionList[i];

            if (!preIns->isStore())
            {
                Operand *newDef = copyOperand(preIns->getDef());
                replaceMap[preIns->getDef()] = newDef;
                if (preIns->isPHI())
                {
                    PhiInstruction *phi = (PhiInstruction *)phis[calculatePhi];
                    if (phi->getSrcs().count(bb)) {
                        nextInstructionList[i] = (Instruction *)(new BinaryInstruction(BinaryInstruction::ADD, newDef, phi->getSrcs()[bb], new Operand(new ConstantSymbolEntry(preIns->getDef()->getType(), 0)), nullptr));
                        notReplaceOp.push_back(newDef);
                        calculatePhi++;
                    }
                }
                else
                {
                    nextIns->setDef(newDef);
                }
            }
        }

        for (auto nextIns : nextInstructionList)
        {
            if (!nextIns->hasNoDef() && count(notReplaceOp.begin(), notReplaceOp.end(), nextIns->getDef()))
            {
                continue;
            }
            for (auto useOp : nextIns->getUses())
            {
                if (replaceMap.find(useOp) != replaceMap.end())
                {
                    nextIns->replaceUsesWith(useOp, replaceMap[useOp]);
                }
                else
                {
                    useOp->addUse(nextIns);
                }
            }
        }

        for (auto ins : phis)
        {
            PhiInstruction *phi = (PhiInstruction *)ins;
            Operand *old = phi->getSrcs()[bb];
            Operand *_new = replaceMap[old];
            phi->replaceUsesWith(old, _new);
        }

        // 最后一次才会换 否则不换
        if (k == num - 2)
        {
            // 构建新的map然后再次替换
            std::map<Operand *, Operand *> newMap;
            int i = 0;
            for (auto nextIns : nextInstructionList)
            {
                if (!nextIns->isStore())
                {
                    newMap[nextIns->getDef()] = finalOperands[i];
                    nextIns->setDef(finalOperands[i]);
                    i++;
                }
            }
            for (auto nextIns : nextInstructionList)
            {
                for (auto useOp : nextIns->getUses())
                {
                    if (newMap.find(useOp) != newMap.end())
                    {
                        nextIns->replaceUsesWith(useOp, newMap[useOp]);
                    }
                }
                bb->insertBefore(nextIns, cmp);
            }
            for (auto ins : phis)
            {
                PhiInstruction *phi = (PhiInstruction *)ins;
                Operand *old = phi->getSrcs()[bb];
                Operand *_new = newMap[old];
                phi->replaceUsesWith(old, _new);
            }
        }
        else
        {
            for (auto nextIns : nextInstructionList)
            {
                bb->insertBefore(nextIns, cmp);
            }
        }

        // 清空原来的
        preInstructionList.clear();
        // 复制新的到pre
        preInstructionList.assign(nextInstructionList.begin(), nextInstructionList.end());
        // 清空next
        nextInstructionList.clear();
        for (auto preIns : preInstructionList)
        {
            nextInstructionList.push_back(preIns->copy());
        }
    }
    // 如果是完全张开的话
    if (ifall)
    {
        // 去除块中的比较跳转指令，并且修改phi即可
        for (auto phi : phis)
        {
            PhiInstruction *p = (PhiInstruction *)phi;
            Operand *phiOp;
            for (auto item : p->getSrcs())
            {
                if (item.first != bb)
                {
                    phiOp = item.second;
                }
            }
            BinaryInstruction *newDefBin = new BinaryInstruction(BinaryInstruction::ADD, phi->getDef(), phiOp, new Operand(new ConstantSymbolEntry(phiOp->getEntry()->getType(), 0)), nullptr);
            Instruction *phiNext = phi->getNext();
            bb->remove(phi);
            bb->insertBefore(newDefBin, phiNext);
            delete phi;
        }

        CondBrInstruction *cond = (CondBrInstruction *)(cmp->getNext());
        UncondBrInstruction *newUnCond = new UncondBrInstruction(cond->getFalseBranch(), nullptr);
        delete cmp;
        delete cond;
        // bb->remove(cmp);
        // bb->remove(cond);
        bb->insertBack(newUnCond);
        bb->removePred(bb);
        bb->removeSucc(bb);
    }

    std::cout << "SpeacialUnroll Finished!\n";
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
        if (!resIns->isStore())
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
    // for (auto bodyinstr = loop->header_bb->begin(); bodyinstr != loop->header_bb->end(); bodyinstr = bodyinstr->getNext())
    //     if (bodyinstr->isCall()) 
    //         return;

    CmpInstruction* LoopCmp = (CmpInstruction*)(loop->exiting_bb->rbegin()->getUses()[0]->getDef());
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
    std::set<Instruction*> Instr_Set;
    for (auto ip : Instr_path)
        Instr_Set.insert(ip);
    int begin = -1, end = -1, stride = -1;
    bool IsBeginCons, IsEndCons, IsStrideCons;
    IsBeginCons = IsEndCons = IsStrideCons = false;
    Operand *endOp = nullptr, *beginOp = nullptr, *strideOp = nullptr;
    BasicBlock *cond = loop->exiting_bb, *body = loop->header_bb;
    CmpInstruction *condCmp = (CmpInstruction*)cond->rbegin()->getPrev();
    assert(condCmp->isCmp());
    int cmpCode = condCmp->getOpcode();
    if (condCmp == nullptr || cmpCode == CmpInstruction::E || cmpCode == CmpInstruction::NE)
    {
        std::cout << "condCmp is null or cmpType is not match" << std::endl;
        return;
    }
    auto uses_cmp = condCmp->getUses();
    Operand* Useop1 = uses_cmp[0], *Useop2 = uses_cmp[1];
    auto Add_Instr1 = Useop1->getDef(), Add_Instr2 = Useop2->getDef();
    bool needReverse = false;
    assert(Useop1 != nullptr && Useop2 != nullptr);
    if (Instr_Set.count(Add_Instr1)) {
        strideOp = Useop1;
        endOp = Useop2;
        if (cmpCode == CmpInstruction::G || cmpCode == CmpInstruction::GE) 
            needReverse = true;
    }
    else if (Instr_Set.count(Add_Instr2)) {
        strideOp = Useop2;
        endOp = Useop1;
        if (cmpCode == CmpInstruction::L || cmpCode == CmpInstruction::LE) 
            needReverse = true;
    }
     
    assert(strideOp != nullptr);
    PhiInstruction* head_phi = (PhiInstruction*)ind_var->du_chains[0][0];
    if (head_phi->getSrcs().size() > 2) {
        std::cout << "Complicated sample, do nothing\n" << std::endl;
        return;
    }
    for (auto srcs : head_phi->getSrcs())
        if (srcs.first != loop->exiting_bb)
            beginOp = srcs.second;
    
    if (needReverse)
        std::swap(beginOp, endOp);
    assert(beginOp != nullptr);
    std::cout << "BeginOp is " << beginOp->toStr() << " , EndOp is " << endOp->toStr() << "\n";
    Operand* step = nullptr;
    Instruction* binary_instr = ind_var->du_chains[0][1];
    if (!binary_instr->isBinary())
    {
        std::cout << "The Induction Change is not east to process";
        return;
    }
    int ivOpcode = binary_instr->getOpcode();
    for (auto useOp : binary_instr->getUses())
    {
        auto se = (IdentifierSymbolEntry *)useOp->getEntry();
        if (se->isConstant() || se->isParam())
            step = useOp;
        else if (useOp->getDef()->getParent() != body)
            step = useOp;
    }
    if (step == nullptr)
    {
        std::cout << "can't get step" << std::endl;
        return;
    }

    if (beginOp->getEntry()->isConstant())
    {
        IsBeginCons = true;
        ConstantSymbolEntry *se = (ConstantSymbolEntry *)(beginOp->getEntry());
        begin = se->getValue();
    }

    if (endOp->getEntry()->isConstant())
    {
        IsEndCons = true;
        ConstantSymbolEntry *se = (ConstantSymbolEntry *)(endOp->getEntry());
        end = se->getValue();
    }

    if (step->getEntry()->isConstant())
    {
        IsStrideCons = true;
        ConstantSymbolEntry *se = (ConstantSymbolEntry *)(step->getEntry());
        stride = se->getValue();
    }

    if (ivOpcode == BinaryInstruction::SUB || ivOpcode == BinaryInstruction::DIV || ivOpcode == BinaryInstruction::MOD) 
    {
        std::cout << "BinaryInstruction Type is SUB, DIV or MOD, do nothing" << std::endl;
        return;
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
                for (int i = begin; i <= end; i += stride )
                    count ++;
                break;
            case CmpInstruction::L:
            case CmpInstruction::G:
                for (int i = begin; i < end; i += stride)
                    count ++;
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
                for (int i = begin; i <= end; i *= stride )
                    count ++;
                break;
            case CmpInstruction::L:
            case CmpInstruction::G:
                for (int i = begin; i < end; i *= stride)
                    count ++;
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
