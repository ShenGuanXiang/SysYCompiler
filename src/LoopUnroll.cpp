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
    auto cand = FindCandidateLoop();
    fprintf(stderr, "loop be unrolled\n");
    for (auto candidate : cand)
        Unroll(candidate);
    fprintf(stderr, "loop info finish\n");
}

/*
    Find Candidate Loop, don't consider instrs that have FuncCall temporarily;
*/
std::vector<Loop *> LoopUnroll::FindCandidateLoop()
{
    std::vector<Loop *> Worklist;
    for (auto f : unit->getFuncList())
    {
        auto analyzer = LoopAnalyzer(f);
        analyzer.analyze();
        for (auto loop : analyzer.getLoops())
        {
            Worklist.push_back(loop);
        }
    }
    return Worklist;
}

bool LoopUnroll::isRegionConst(Operand *i, Operand *c)
{
    // 常数
    auto se = (IdentifierSymbolEntry *)(c->getEntry());
    if (se->isConstant())
    {
        return true;
    }
    else if (se->isGlobal())
    {
        return false;
    }
    else if (se->isParam())
    {
        return true;
    }
    else
    {

        if (c->getDef() == nullptr || i->getDef() == nullptr)
        {
            std::cout << "region count def is null" << std::endl;
        }
        BasicBlock *c_Farther = c->getDef()->getParent();
        BasicBlock *i_Farther = i->getDef()->getParent();
        i_Farther->getParent()->ComputeDom();
        auto Dom_i_Father = i_Farther->getSDoms();
        if (Dom_i_Father.count(c_Farther))
        {
            return true;
        }
    }
    return false;
}

Operand *LoopUnroll::getBeginOp(BasicBlock *bb, Operand *strideOp, std::stack<Instruction *> &Insstack)
{
    Operand *temp = strideOp;

    while (!temp->getDef()->isPHI())
    {
        Instruction *tempdefIns = temp->getDef();
        Insstack.push(tempdefIns);
        int num;
        std::vector<Operand *> uses = tempdefIns->getUses();
        bool iftempChange = false;

        if (tempdefIns->getUses().size() != 2)
        {
            std::cout << "can't find phi" << std::endl;
            return nullptr;
        }

        Operand *useOp1 = tempdefIns->getUses()[0], *useOp2 = tempdefIns->getUses()[1];

        if (isRegionConst(useOp1, useOp2))
        {
            temp = useOp1;
            iftempChange = true;
        }
        else if (isRegionConst(useOp2, useOp1))
        {
            temp = useOp2;
            iftempChange = true;
        }

        if (!iftempChange || (temp->getDef()->getParent() != bb))
        {
            // 没有改变，则。。。。
            // temp定义出错
            std::cout << "temp no change or temp def bb not right" << std::endl;
            return nullptr;
        }
    }

    PhiInstruction *phi = (PhiInstruction *)temp->getDef();
    Insstack.push(temp->getDef());
    Operand *beginOp;
    for (auto item : phi->getSrcs())
    {
        if (item.first != bb)
        {
            beginOp = item.second;
        }
    }
    return beginOp;
}

void LoopUnroll::specialCopyInstructions(BasicBlock *bb, int num, Operand *endOp, Operand *strideOp, bool ifall)
{
    std::vector<Instruction *> preInstructionList;
    std::vector<Instruction *> nextInstructionList;
    std::vector<Instruction *> phis;
    std::vector<Instruction *> copyPhis;
    CmpInstruction *cmp;
    Operand *cmpOld = strideOp;
    std::vector<Operand *> finalOperands;
    std::map<Operand *, Operand *> begin_final_Map;
    // Find all phis and insert instrs in preInstructionList
    for (auto bodyinstr = bb->begin(); bodyinstr != bb->end(); bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isPHI())
        {
            phis.push_back(bodyinstr);
        }
        else if (bodyinstr->isCmp())
        {
            cmp = (CmpInstruction *)bodyinstr;
            break;
        }
        preInstructionList.push_back(bodyinstr);
    }
    copyPhis.assign(phis.begin(), phis.end());
    copyPhis.assign(phis.begin(), phis.end());

    // Insert next_instr into nextInstructionList
    for (auto preIns : preInstructionList)
    {
        Instruction *ins = preIns->copy();
        if (!preIns->isStore())
        {
            Operand *newDef = new Operand(new TemporarySymbolEntry(preIns->getDef()->getType(), SymbolTable::getLabel()));
            begin_final_Map[preIns->getDef()] = newDef;
            finalOperands.push_back(preIns->getDef());
            ins->setDef(newDef);
            preIns->setDef(newDef);
        }
        nextInstructionList.push_back(ins);
    }

    for (int i = 0; i < preInstructionList.size(); i++)
    {
        Instruction *preIns = preInstructionList[i];
        for (auto useOp : preIns->getUses())
        {
            if (begin_final_Map.find(useOp) != begin_final_Map.end())
            {
                preIns->replaceUsesWith(useOp, begin_final_Map[useOp]);
            }
        }
    }

    for (auto nextIns : nextInstructionList)
    {
        for (auto useOp : nextIns->getUses())
        {
            if (begin_final_Map.find(useOp) != begin_final_Map.end())
            {
                nextIns->replaceUsesWith(useOp, begin_final_Map[useOp]);
            }
            else
            {
                useOp->addUse(nextIns);
            }
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
                Operand *newDef = new Operand(new TemporarySymbolEntry(preIns->getDef()->getType(), SymbolTable::getLabel()));
                replaceMap[preIns->getDef()] = newDef;
                if (count(copyPhis.begin(), copyPhis.end(), preIns))
                {
                    PhiInstruction *phi = (PhiInstruction *)phis[calculatePhi];
                    nextInstructionList[i] = (Instruction *)(new BinaryInstruction(BinaryInstruction::ADD, newDef, phi->getSrcs()[bb], new Operand(new ConstantSymbolEntry(preIns->getDef()->getType(), 0)), nullptr));
                    notReplaceOp.push_back(newDef);
                    calculatePhi++;
                    copyPhis.push_back(nextInstructionList[i]);
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

        // Operand* cmpNew=replaceMap[cmpOld];
        // cmp->replaceUse(cmpOld,cmpNew);
        // cmpOld=cmpNew;

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
        // std::map<Operand*, Operand*> tempMap;
        // for(auto notReOp:notReplaceOp){
        //     for(auto item:replaceMap){
        //         if(item.second==notReOp){
        //             tempMap[item.first]=item.second;
        //         }
        //     }
        // }

        // for(auto item:tempMap){
        //     replaceMap[item.first]=item.second;
        // }
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
        }

        CondBrInstruction *cond = (CondBrInstruction *)cmp->getNext();
        UncondBrInstruction *newUnCond = new UncondBrInstruction(cond->getFalseBranch(), nullptr);
        bb->remove(cmp);
        bb->remove(cond);
        bb->insertBack(newUnCond);
        bb->removePred(bb);
        bb->removeSucc(bb);
    }
}

// 只考虑+1的情况先 不管浮点？
void LoopUnroll::normalCopyInstructions(BasicBlock *condbb, BasicBlock *bodybb, Operand *beginOp, Operand *endOp, Operand *strideOp)
{
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
    CmpInstruction *cmp;
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

void LoopUnroll::Unroll(Loop *loopstruct)
{
    bool HasCallInBody = false;
    for (auto bodyinstr = loopstruct->header_bb->begin(); bodyinstr != loopstruct->header_bb->end(); bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isCall())
        {
            HasCallInBody = true;
            break;
        }
    }
    if (HasCallInBody)
    {
        // Exception("Candidate loop shall have no call in body");
        // std::cout<<"Candidate loop shall have no call in body"<<std::endl;
        return;
    }
    /*
     * [Step 1] Calc begin, end, stride. They shall all be constant in Special LoopUnrolling.
     * [Step 2] Choose Normal LoopUnrolling or Special LoopUnrolling.
     * [Step 2.1] IF begin, end and stride are all constant, try Special LoopUnrolling.
     * [Step 2.2] IF stride is constant, while begin or end is not constant, try Normal LoopUnrolling.
     *
     *                     --------
     *                     ↓      ↑
     * Special:  pred -> cond -> body  Exitbb      ==>     pred -> newbody -> Exitbb
     *                     ↓             ↑
     *                     ---------------
     *
     *
     *
     *                     --------                                    ------------        -----------
     *                     ↓      ↑                                    ↓          ↑        ↓         ↑
     * Normal:  pred ->  cond -> body  Exitbb      ==>     pred -> rescond -> resbody maincond -> mainbody Exitbb
     *                     ↓             ↑                             ↓                  ↑   ↓              ↑
     *                     ---------------                             --------------------   ----------------
     *
     * 先判断除以四 或者 二 八 取模
     */
    int begin = -1, end = -1, stride = -1;
    bool IsBeginCons, IsEndCons, IsStrideCons;
    IsBeginCons = IsEndCons = IsStrideCons = false;
    BasicBlock *cond = loopstruct->exiting_bb, *body = loopstruct->header_bb;
    CmpInstruction *cmp;
    // 可以计算出基本归纳变量表存储信息 利用信息

    Operand *endOp, *beginOp, *strideOp;

    // cal begin end stride
    // 都是const
    // 获取所有的归纳变量 如果所有的归纳变量对应的初始
    // 直接利用cmp确定即可
    // 由SSA图再找stride
    bool ifcmpInsMatch = true;
    CmpInstruction *condCmp = nullptr;
    CmpInstruction *bodyCmp = nullptr;
    assert(cond->rbegin()->getPrev()->isCmp());
    condCmp = (CmpInstruction *)cond->rbegin()->getPrev();
    int opcode = condCmp->getOpcode();
    switch (opcode)
    {
    case CmpInstruction::G:
        /* code */

        break;
    case CmpInstruction::GE:
        /* code */

        break;
    case CmpInstruction::L:
        /* code */

        break;
    case CmpInstruction::LE:
        /* code */
        break;

    default:
        ifcmpInsMatch = false;
        break;
    }

    if (!ifcmpInsMatch)
    {
        return;
    }

    std::stack<Instruction *> Insstack;

    for (auto bodyinstr = body->begin(); bodyinstr != body->end(); bodyinstr = bodyinstr->getNext())
    {
        if (bodyinstr->isCmp())
        {
            bodyCmp = (CmpInstruction *)bodyinstr;
            Operand *cmpOp1 = bodyCmp->getUses()[0];
            Operand *cmpOp2 = bodyCmp->getUses()[1];
            int opcode = bodyCmp->getOpcode();
            switch (opcode)
            {
            case CmpInstruction::G:
                /* code */
                endOp = cmpOp1;
                strideOp = cmpOp2;
                if (strideOp->getType()->isConst())
                {
                    strideOp = cmpOp1;
                    endOp = cmpOp2;
                }
                beginOp = getBeginOp(body, strideOp, Insstack);
                break;
            case CmpInstruction::GE:
                /* code */
                endOp = cmpOp1;
                strideOp = cmpOp2;
                if (strideOp->getType()->isConst())
                {
                    strideOp = cmpOp1;
                    endOp = cmpOp2;
                }
                beginOp = getBeginOp(body, strideOp, Insstack);
                break;
            case CmpInstruction::L:
                /* code */
                endOp = cmpOp2;
                strideOp = cmpOp1;
                if (strideOp->getType()->isConst())
                {
                    strideOp = cmpOp2;
                    endOp = cmpOp1;
                }
                beginOp = getBeginOp(body, strideOp, Insstack);
                break;
            case CmpInstruction::LE:
                /* code */
                endOp = cmpOp2;
                strideOp = cmpOp1;
                if (strideOp->getType()->isConst())
                {
                    strideOp = cmpOp2;
                    endOp = cmpOp1;
                }
                beginOp = getBeginOp(body, strideOp, Insstack);
                break;
            default:
                std::cout << "bodycmp is ne or e" << std::endl;
                break;
            }
        }
    }

    if (beginOp == nullptr)
    {
        std::cout << "begin op is null" << std::endl;
        return;
    }
    // 先考虑一种特殊情况
    // 归纳变量只变换一次
    int ivOpcode;
    Operand *step = nullptr;

    if (Insstack.size() == 2)
    {
        //
        Instruction *topIns = Insstack.top();
        if (topIns->isPHI())
        {
            PhiInstruction *phi = (PhiInstruction *)topIns;
            Insstack.pop();
            Instruction *ins = Insstack.top();
            if (ins->isBinary())
            {
                ivOpcode = ins->getOpcode();
                for (auto useOp : ins->getUses())
                {
                    auto se = (IdentifierSymbolEntry *)useOp->getEntry();
                    if (se->isConstant() || se->isParam())
                    {
                        step = useOp;
                    }
                    else if (useOp->getDef()->getParent() != body)
                    {
                        step = useOp;
                    }
                }
            }
            else
            {
                std::cout << "the iv ins not bin" << std::endl;
                return;
            }
        }
        else
        {
            std::cout << "the top ins in stack is not phi" << std::endl;
            return;
        }
    }
    else
    {
        std::cout << "not normal" << std::endl;
        return;
    }

    if (step == nullptr)
    {
        std::cout << "can't not get step" << std::endl;
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

    if (IsBeginCons && IsEndCons && IsStrideCons)
    {
        // 完全展开
        // 计算多少次
        if (ivOpcode == BinaryInstruction::ADD)
        {
            if (bodyCmp->getOpcode() == CmpInstruction::G || bodyCmp->getOpcode() == CmpInstruction::L)
            {
                int count = 0;
                for (int i = begin; i < end; i = i + stride)
                {
                    count++;
                }
                // 指令copy count 份
                // body中的跳转指令不copy
                // 循环内部是小于count 所以count初始值直接设置为0即可
                if (count <= MAXUNROLLNUM)
                    specialCopyInstructions(body, count, endOp, strideOp, true);
                else
                { // 特殊展开
                }
            }
            else if (bodyCmp->getOpcode() == CmpInstruction::GE || bodyCmp->getOpcode() == CmpInstruction::LE)
            {
                int count = 0;
                for (int i = begin; i <= end; i = i + stride)
                {
                    count++;
                }
                // 循环内部是小于count 所以count初始值直接设置为0即可
                if (count <= MAXUNROLLNUM)
                    specialCopyInstructions(body, count, endOp, strideOp, true);
                else
                { // 特殊展开
                }
            }
            else
            {
                std::cout << "cmp not match" << std::endl;
            }
        }
        else if (ivOpcode == BinaryInstruction::SUB)
        {
        }
        else if (ivOpcode == BinaryInstruction::MUL)
        {
            std::cout << "mul" << std::endl;
            if (bodyCmp->getOpcode() == CmpInstruction::G || bodyCmp->getOpcode() == CmpInstruction::L)
            {
                int count = 0;
                for (int i = begin; i < end; i = i * stride)
                {
                    count++;
                }
                // 指令copy count 份
                // body中的跳转指令不copy
                // 循环内部是小于count 所以count初始值直接设置为0即可
                if (count <= MAXUNROLLNUM)
                    specialCopyInstructions(body, count, endOp, strideOp, true);
                else
                { // 特殊展开
                }
            }
            else if (bodyCmp->getOpcode() == CmpInstruction::GE || bodyCmp->getOpcode() == CmpInstruction::LE)
            {
                int count = 0;
                for (int i = begin; i <= end; i = i * stride)
                {
                    count++;
                }
                // 循环内部是小于count 所以count初始值直接设置为0即可
                if (count <= MAXUNROLLNUM)
                    specialCopyInstructions(body, count, endOp, strideOp, true);
                else
                { // 特殊展开
                }
            }
            else
            {
                std::cout << "cmp not match" << std::endl;
            }
        }
        else if (ivOpcode == BinaryInstruction::DIV)
        {
        }
        else
        {
            fprintf(stderr, "stride calculate not add sub mul div\n");
            return;
        }
    }
    else if (IsStrideCons)
    {
        // 展开四次
        // copy四次
        // 构建rescond resbody
        // rescond包含 最后算出来的变量值 然后新建一条cmp指令 最后算出来的变量值与end作比较，重构一个循环即可
        // 看n是否为temp
        // 后续的stride继续补充即可
        if (ivOpcode == BinaryInstruction::ADD)
        {
            if (stride == 1)
            {
                normalCopyInstructions(cond, body, beginOp, endOp, strideOp);
            }
        }
    }
}
