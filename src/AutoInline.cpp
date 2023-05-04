#include "AutoInline.h"
#include <unordered_map>

void AutoInliner::Print_Funcinline(std::queue<Function *>& func_inline) {
    fprintf(stderr, "Now Func_inline queue Has ");
    std::queue<Function *> temp;
    while (!func_inline.empty()) {
        auto t = func_inline.front();
        temp.push(t);
        func_inline.pop();
        fprintf(stderr, "%s ", ((IdentifierSymbolEntry*)t->getSymPtr())->getName().c_str());
    }
    fprintf(stderr, "\n");
    func_inline.swap(temp);
}

void Unit::getCallGraph()
{
    for (auto f : this->getFuncList())
    {
        f->getCallees().clear();
        f->getCallers().clear();
        f->getCallers_instr().clear();
        f->getdegree() = 0;
    }
    for (auto f : this->getFuncList())
    {
        for (auto bb : f->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            {
                if (instr->isCall())
                {
                    auto func_se = ((FuncCallInstruction *)instr)->GetFuncSe();
                    if (!func_se->isLibFunc())
                    {
                        if (f != func_se->getFunction() 
                            && 
                            !f->getCallees().count(func_se->getFunction())) 
                            f->getdegree() ++;
                        f->getCallees().insert(func_se->getFunction());
                        func_se->getFunction()->getCallers().insert(f);
                        func_se->getFunction()->getCallers_instr().insert(instr);
                    }
                }
            }
    }
}

Operand* getTempOperand(Operand* ope) {
    return new Operand(
        new TemporarySymbolEntry(ope->getType(), SymbolTable::getLabel()));
}

// 在这里之判断是否是递归函数，如果是递归函数的话就不展开，日后可以在这个位置加一些对于可以内连的判断
bool AutoInliner::ShouldBeinlined(Function *f)
{
    return !f->isRecur();
}

void AutoInliner::pass()
{
    DeadCodeElim dce(unit);
    dce.pass();
    CallIntrNum();
    RecurDetect();
    std::queue<Function *> func_inline;
    for (auto f : unit->getFuncList())
        if (!((IdentifierSymbolEntry *)f->getSymPtr())->isLibFunc() &&
            !((IdentifierSymbolEntry *)f->getSymPtr())->isMain())
            if (!f->getdegree())
                func_inline.push(f);

    Print_Funcinline(func_inline);
    while (!func_inline.empty())
    {
        auto f = func_inline.front();
        func_inline.pop();
        if (((IdentifierSymbolEntry*)f->getSymPtr())->isMain()) continue;
        if (!ShouldBeinlined(f)) 
        {
            for (auto ff : f->getCallers()) {
                ff->getdegree()--;
                if (!ff->getdegree()) func_inline.push(ff);
            }
            continue;
        }
        std::vector<Instruction*> need_pass;
        for (auto itr : f->getCallers_instr()) {
            need_pass.push_back(itr);
            auto Func = itr->getParent()->getParent();
            if (Func->getCallees().count(f)) {
                Func->getCallees().erase(f);
                Func->getdegree() --;
                if (!Func->getdegree()) 
                    func_inline.push(Func);
            }
        }
        for (auto instr : need_pass)
            pass(instr);
        Print_Funcinline(func_inline);
    }
    dce.deleteUselessFunc();
}

void AutoInliner::pass(Instruction* instr)
{
    auto func_se = ((FuncCallInstruction*)instr)->GetFuncSe();
    auto func = func_se->getFunction(), Func = instr->getParent()->getParent();
    if (func_se->isLibFunc() || func == nullptr) return;
    auto instr_bb = instr->getParent(), exit_bb = new BasicBlock(Func);
    auto Ret = ((FuncCallInstruction*)instr)->getDef();
    auto params = ((FuncCallInstruction*)instr)->getUses();

    auto nxt_first = instr->getNext(), nxt_last = instr_bb->rbegin(), nxt_head = exit_bb->end();
    nxt_head->setNext(nxt_first);
    nxt_first->setPrev(nxt_head);
    nxt_last->setNext(nxt_head);
    nxt_head->setPrev(nxt_last);
    instr->setNext(instr_bb->end());
    instr_bb->end()->setPrev(instr);

    for (auto in1 = exit_bb->begin(); in1 != exit_bb->end(); in1 = in1->getNext())
        in1->setParent(exit_bb);
    while (!instr_bb->succEmpty()) {
        auto succ = *(instr_bb->succ_begin());
        succ->removePred(instr_bb);
        instr_bb->removeSucc(succ);
        succ->addPred(exit_bb);
        exit_bb->addSucc(succ);
        std::map<BasicBlock*, std::vector<BasicBlock*>> temp;
        temp[instr_bb] = {exit_bb};
        auto in1 = succ->begin();
        if (in1->isPHI()) {
            auto phi = (PhiInstruction*)in1;
            phi->addEdge(exit_bb, phi->getSrcs()[instr_bb]);
            phi->removeEdge(instr_bb);
        }
    }


    BasicBlock* entry;
    std::map<Operand*, Operand*> ope2ope;
    std::map<BasicBlock*, BasicBlock*> block2block;
    std::vector<BasicBlock*> retBlocks;
    std::vector<Operand*> retOpes;
    std::vector<Instruction*> branchs;

    std::map<Instruction*, Instruction*> phis;

    for (auto block : func->getBlockList()) {
        auto newBlock = new BasicBlock(Func);
        if (block == func->getEntry())
            entry = newBlock;
        block2block[block] = newBlock;
        for (auto in1 = block->begin(); in1 != block->end(); in1 = in1->getNext()) {
            Instruction* new_in;
            if (in1->isRet()) {
                retBlocks.push_back(newBlock);
                auto uses = in1->getUses();
                if (!uses.empty()) {
                    auto use = uses[0];
                    if (!use->getEntry()->isConstant()) {
                        auto zero =
                            new Operand(new ConstantSymbolEntry(use->getType(), 0));
                        auto dst = getTempOperand(use);
                        retOpes.push_back(dst);
                        Operand* src;
                        if (ope2ope.count(use))
                            src = ope2ope[use];
                        else {
                            src = getTempOperand(use);
                            ope2ope[use] = src;
                        }
                        new_in = new BinaryInstruction(BinaryInstruction::ADD, dst,
                                                    src, zero);
                        new_in->setParent(newBlock);
                        newBlock->insertBack(new_in);
                    }
                    else retOpes.push_back(use);
                }
                new UncondBrInstruction(exit_bb, newBlock);
            }
            else
            {
                new_in = in1->copy();
                new_in->setParent(newBlock);
                switch (new_in->getInstType())
                {
                    case Instruction::PHI: {
                        phis.insert({new_in, in1});
                        break;
                    }
                    case Instruction::UNCOND:
                    case Instruction::COND: {
                        branchs.push_back(new_in);
                        break;
                    }
                    default: {
                        Operand* def = nullptr;
                        if (!in1->hasNoDef()) {
                            def = in1->getDef();
                            Operand* dst;
                            if (ope2ope.find(def) != ope2ope.end())
                                dst = ope2ope[def];
                            else {
                                dst = getTempOperand(def);
                                ope2ope[def] = dst;
                            }
                            new_in->setDef(dst);
                        }
                        auto uses = in1->getUses();
                        for (auto use : uses) {
                            Operand* src;
                            if (use->getEntry()->isTemporary()) {
                                if (ope2ope.find(use) != ope2ope.end())
                                    src = ope2ope[use];
                                else {
                                    src = getTempOperand(use);
                                    ope2ope[use] = src;
                                }
                            }
                            else if (use->getEntry()->isVariable()) {
                                if (((IdentifierSymbolEntry*)use->getEntry())->isParam()) {
                                    int no = ((IdentifierSymbolEntry*)use->getEntry())->getParamNo();
                                    src = params[no];
                                } else if (((IdentifierSymbolEntry*)use->getEntry())->isGlobal()) {
                                    src = use;
                                } else {
                                    if (ope2ope.find(use) != ope2ope.end())
                                        src = ope2ope[use];
                                    else {
                                        src = getTempOperand(use);
                                        ope2ope[use] = src;
                                    }
                                }
                            }
                            else src = use;
                            new_in->replaceUsesWith(use, src);
                        }
                    }
                }
                newBlock->insertBack(new_in);
            }
        }
    }

    for (auto block : func->getBlockList()) {
        auto newBlock = block2block[block];
        for (auto it = block->pred_begin(); it != block->pred_end(); it++)
            newBlock->addPred(block2block[*it]);
        for (auto it = block->succ_begin(); it != block->succ_end(); it++)
            newBlock->addSucc(block2block[*it]);
    }

    for (auto in : branchs) {
        if (in->isCond()) {
            auto use = in->getUses()[0];
            Operand* src;
            if (ope2ope.count(use))
                src = ope2ope[use];
            else {
                src = getTempOperand(use);
                ope2ope[use] = src;
            }
            in->replaceUsesWith(use, src);
            auto cond = (CondBrInstruction*)in;
            cond->setTrueBranch(block2block[cond->getTrueBranch()]);
            cond->setFalseBranch(block2block[cond->getFalseBranch()]);
        }
        if (in->isUncond()) {
            auto unCond = (UncondBrInstruction*)in;
            unCond->setBranch(block2block[unCond->getBranch()]);
        }
    }

    for (auto it : phis) {
        auto oldPhi = (PhiInstruction*)(it.second);
        auto newPhi = (PhiInstruction*)(it.first);
        newPhi->getSrcs().clear();
        newPhi->getUses().clear();
        newPhi->get_incomplete() = false;
        for (auto it : oldPhi->getSrcs()) {
            Operand* src;
            auto use = it.second;
            if (use->getEntry()->isConstant()) {
                src = use;
            } else {
                if (ope2ope.count(use))
                    src = ope2ope[use];
                else {
                    src = getTempOperand(use);
                    ope2ope[use] = src;
                }
            }
            newPhi->addEdge(block2block[it.first], src);
        }
        auto def = oldPhi->getDef();
        Operand* dst;
        if (ope2ope.count(def))
            dst = ope2ope[def];
        else {
            dst = getTempOperand(def);
            ope2ope[def] = dst;
        }
        newPhi->setDef(dst);
    }

    for (auto block : retBlocks) {
        block->addSucc(exit_bb);
        exit_bb->addPred(block);
    }
    new UncondBrInstruction(entry, instr_bb);
    instr_bb->addSucc(entry);
    entry->addPred(instr_bb);
    if (Ret && !dynamic_cast<FunctionType *>(func->getSymPtr()->getType())->getRetType()->isVoid()) {
        Instruction* newIn;
        int size = retOpes.size();
        if (size > 1) {
            PhiInstruction* phi = new PhiInstruction(Ret);
            phi->get_incomplete() = false;
            for (int i = 0; i < size; i++)
                phi->addEdge(retBlocks[i], retOpes[i]);
            newIn = phi;
        } else {
            fprintf(stderr, "FuncName is %s\n", func_se->getName().c_str());
            auto funcType = dynamic_cast<FunctionType *>(func->getSymPtr()->getType());
            auto zero = new Operand(new ConstantSymbolEntry(funcType->getRetType(), 0));
            newIn = new BinaryInstruction(BinaryInstruction::ADD, Ret,
                                        retOpes[0], zero);
        }
        newIn->setParent(exit_bb);
        exit_bb->insertFront(newIn);
    }
    instr_bb->remove(instr);
}

void AutoInliner::CallIntrNum()
{
    for (auto it = unit->begin(); it != unit->end(); it++)
    {
        int num = 0;
        auto func = *it;
        for (auto block : func->getBlockList())
            for (auto in = block->begin(); in != block->end();
                 in = in->getNext())
                if (in->isCalc())
                    num++;
        func->SetCalcInstNum(num);
    }
}

void AutoInliner::RecurDetect()
{
    unit->getCallGraph();
    for (auto cur_f : unit->getFuncList())
    {
        if (cur_f->isRecur())
            continue;
        std::set<Function *> Path;
        UpdateRecur(cur_f, Path);
    }
}

void AutoInliner::UpdateRecur(Function *f, std::set<Function *> &Path)
{
    for (auto f_nxt : f->getCallees())
        if (Path.count(f_nxt))
        {
            f_nxt->SetRecur();
            return;
        }
        else
        {
            Path.insert(f_nxt);
            UpdateRecur(f_nxt, Path);
            Path.erase(f_nxt);
        }
}