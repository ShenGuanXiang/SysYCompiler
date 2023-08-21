#include "AutoInline.h"
#include <unordered_map>

static const int maxInlineIter = 1, maxRecurCall = 10;

void AutoInliner::ReplSingleUseWith(Instruction *inst, int i, Operand *new_op)
{
    inst->getUses()[i]->removeUse(inst);
    inst->getUses()[i] = new_op;
    new_op->addUse(inst);
}

void AutoInliner::Print_Funcinline(std::queue<Function *> &func_inline)
{
    fprintf(stderr, "Now Func_inline queue Has ");
    std::queue<Function *> temp;
    while (!func_inline.empty())
    {
        auto t = func_inline.front();
        temp.push(t);
        func_inline.pop();
        fprintf(stderr, "%s ", ((IdentifierSymbolEntry *)t->getSymPtr())->getName().c_str());
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
        f->getCallersInsts().clear();
        f->getCalleesInsts().clear();
    }
    for (auto f : this->getFuncList())
    {
        for (auto bb : f->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            {
                if (instr->isCall())
                {
                    auto func_se = ((FuncCallInstruction *)instr)->getFuncSe();
                    if (!func_se->isLibFunc())
                    {
                        f->getCallees().insert(func_se->getFunction());
                        func_se->getFunction()->getCallers().insert(f);
                        func_se->getFunction()->getCallersInsts().insert(instr);
                        f->getCalleesInsts().insert(instr);
                    }
                }
            }
    }
}

void AutoInliner::InitDegree()
{
    degree.clear();
    for (auto f : unit->getFuncList())
    {
        degree[f] = f->getCallees().size();
        if (f->getCallees().count(f))
            degree[f]--;
        // degree[f] = f->getCalleesInst().size();
    }
}

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

// 在这里之判断是否是递归函数，如果是递归函数的话就不展开，日后可以在这个位置加一些对于可以内连的判断
bool AutoInliner::ShouldBeinlined(Function *f)
{
    return !is_recur[f];
}
void AutoInliner::pass(bool recur_inline)
{
    SimplifyCFG sc(unit);
    sc.pass();
    unit->getCallGraph();
    InitDegree();
    RecurDetect();
    std::queue<Function *> func_inline;
    for (auto f : unit->getFuncList())
        if (!((IdentifierSymbolEntry *)f->getSymPtr())->isLibFunc() &&
            !((IdentifierSymbolEntry *)f->getSymPtr())->isMain())
            if (!degree[f])
                func_inline.push(f);

    while (!func_inline.empty())
    {
        // Print_Funcinline(func_inline);
        auto f = func_inline.front();
        func_inline.pop();
        if (((IdentifierSymbolEntry *)f->getSymPtr())->isMain())
            continue;
        if (!ShouldBeinlined(f))
        {
            for (auto ff : f->getCallers())
            {
                degree[ff]--;
                if (!degree[ff])
                    func_inline.push(ff);
            }
            continue;
        }
        std::set<Function *> f_vec;
        std::vector<Instruction *> need_pass;
        for (auto itr : f->getCallersInsts())
        {
            need_pass.push_back(itr);
            auto Func = itr->getParent()->getParent();
            f_vec.insert(Func);
            degree[Func]--;
            if (!degree[Func])
                func_inline.push(Func);
        }
        for (auto instr : need_pass)
            pass(instr);
    }

    std::vector<Instruction *> allocas;
    for (auto f : unit->getFuncList())
        for (auto bb : f->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
                if (instr->isAlloca())
                    allocas.push_back(instr);
    for (auto instr : allocas)
    {
        auto entry = instr->getParent()->getParent()->getEntry();
        instr->getParent()->remove(instr);
        entry->insertFront(instr);
    }

    if (recur_inline)
    {
        for (auto func : unit->getFuncList())
            RecurInline(func);
    }

    sc.pass();

    DeadCodeElim dce(unit);
    dce.deleteUselessFunc();
}

void AutoInliner::pass(Instruction *instr, Function *deepcopy_func)
{
    auto func_se = ((FuncCallInstruction *)instr)->getFuncSe();
    auto func = deepcopy_func == nullptr ? func_se->getFunction() : deepcopy_func, Func = instr->getParent()->getParent();
    if (func_se->isLibFunc() || func == nullptr)
        return;
    auto instr_bb = instr->getParent(), exit_bb = new BasicBlock(Func);
    auto Ret = ((FuncCallInstruction *)instr)->getDef();
    auto params = ((FuncCallInstruction *)instr)->getUses();

    auto nxt_first = instr->getNext(), nxt_last = instr_bb->rbegin(), nxt_head = exit_bb->end();
    nxt_head->setNext(nxt_first);
    nxt_first->setPrev(nxt_head);
    nxt_last->setNext(nxt_head);
    nxt_head->setPrev(nxt_last);
    instr->setNext(instr_bb->end());
    instr_bb->end()->setPrev(instr);

    for (auto in1 = exit_bb->begin(); in1 != exit_bb->end(); in1 = in1->getNext())
        in1->setParent(exit_bb);
    while (!instr_bb->succEmpty())
    {
        auto succ = *(instr_bb->succ_begin());
        succ->removePred(instr_bb);
        instr_bb->removeSucc(succ);
        succ->addPred(exit_bb);
        exit_bb->addSucc(succ);
        std::map<BasicBlock *, std::vector<BasicBlock *>> temp;
        temp[instr_bb] = {exit_bb};
        auto in1 = succ->begin();
        while (in1 != succ->end() && in1->isPHI())
        {
            auto phi = (PhiInstruction *)in1;
            auto useop = phi->getSrcs()[instr_bb];
            phi->removeEdge(instr_bb);
            phi->addEdge(exit_bb, useop);
            in1 = in1->getNext();
        }
    }

    BasicBlock *entry;
    std::map<Operand *, Operand *> ope2ope;
    std::map<BasicBlock *, BasicBlock *> block2block;
    std::vector<BasicBlock *> retBlocks;
    std::vector<Operand *> retOpes;
    std::vector<Instruction *> branches;

    std::map<Instruction *, Instruction *> phis;

    auto fparams = func->getParamsOp();
    for (auto fparam : fparams)
    {
        ope2ope[fparam] = params[((IdentifierSymbolEntry *)(fparam->getEntry()))->getParamNo()];
    }
    for (auto id_se : unit->getDeclList())
    {
        if (id_se->getType()->isARRAY() || id_se->getType()->isInt() || id_se->getType()->isFloat())
        {
            ope2ope[id_se->getAddr()] = id_se->getAddr();
        }
    }

    auto all_bbs = func->getBlockList();
    for (auto block : all_bbs)
    {
        auto newBlock = new BasicBlock(Func);
        if (block == func->getEntry())
            entry = newBlock;
        block2block[block] = newBlock;
        for (auto in1 = block->begin(); in1 != block->end(); in1 = in1->getNext())
        {
            Instruction *new_in;
            if (in1->isRet())
            {
                retBlocks.push_back(newBlock);
                auto uses = in1->getUses();
                if (!uses.empty())
                {
                    auto use = uses[0];
                    assert(uses.size() == 1);
                    Operand *src;
                    if (ope2ope.count(use))
                        src = ope2ope[use];
                    else
                    {
                        src = copyOperand(use);
                        ope2ope[use] = src;
                    }
                    retOpes.push_back(src);
                }
                new UncondBrInstruction(exit_bb, newBlock);
            }
            else
            {
                new_in = in1->copy();
                new_in->setParent(newBlock);
                switch (new_in->getInstType())
                {
                case Instruction::PHI:
                {
                    phis.insert({new_in, in1});
                }
                case Instruction::UNCOND:
                case Instruction::COND:
                {
                    branches.push_back(new_in);
                }
                default:
                {
                    Operand *def = nullptr;
                    if (!in1->hasNoDef())
                    {
                        def = in1->getDef();
                        Operand *dst;
                        if (ope2ope.find(def) != ope2ope.end())
                            dst = ope2ope[def];
                        else
                        {
                            dst = copyOperand(def);
                            ope2ope[def] = dst;
                        }
                        new_in->setDef(dst);
                    }
                    auto uses = in1->getUses();
                    int use_idx = 0;
                    for (auto use : uses)
                    {
                        Operand *src;
                        if (ope2ope.find(use) != ope2ope.end())
                            src = ope2ope[use];
                        else
                        {
                            src = copyOperand(use);
                            ope2ope[use] = src;
                        }
                        ReplSingleUseWith(new_in, use_idx, ope2ope[use]);
                        use_idx++;
                    }
                }
                }
                if (new_in->isAlloca())
                    entry->insertFront(new_in);
                else if (new_in->isPHI())
                    newBlock->insertFront(new_in);
                else
                    newBlock->insertBack(new_in);
            }
        }
    }

    for (auto block : func->getBlockList())
    {
        auto newBlock = block2block[block];
        for (auto it = block->pred_begin(); it != block->pred_end(); it++)
            newBlock->addPred(block2block[*it]);
        for (auto it = block->succ_begin(); it != block->succ_end(); it++)
            newBlock->addSucc(block2block[*it]);
    }

    for (auto in : branches)
    {
        if (in->isCond())
        {
            auto cond = (CondBrInstruction *)in;
            cond->setTrueBranch(block2block[cond->getTrueBranch()]);
            cond->setFalseBranch(block2block[cond->getFalseBranch()]);
        }
        else if (in->isUncond())
        {
            auto unCond = (UncondBrInstruction *)in;
            unCond->setBranch(block2block[unCond->getBranch()]);
        }
    }

    for (auto it : phis)
    {
        auto oldPhi = (PhiInstruction *)(it.second);
        auto newPhi = (PhiInstruction *)(it.first);

        auto old_srcs = oldPhi->getSrcs();
        for (auto [old_bb, old_op] : old_srcs)
        {
            newPhi->getSrcs().erase(old_bb);
            newPhi->getSrcs()[block2block[old_bb]] = ope2ope[old_op];
        }
    }

    for (auto block : retBlocks)
    {
        block->addSucc(exit_bb);
        exit_bb->addPred(block);
    }
    new UncondBrInstruction(entry, instr_bb);
    instr_bb->addSucc(entry);
    entry->addPred(instr_bb);
    if (Ret && !dynamic_cast<FunctionType *>(func->getSymPtr()->getType())->getRetType()->isVoid())
    {
        Instruction *newIn = nullptr;
        int size = retOpes.size();
        PhiInstruction *phi = new PhiInstruction(Ret, false);

        for (int i = 0; i < size; i++)
            phi->addEdge(retBlocks[i], retOpes[i]);
        newIn = phi;
        newIn->setParent(exit_bb);
        exit_bb->insertFront(newIn);
    }
    delete instr;
}

void AutoInliner::RecurInline(Function *func)
{
    for (int iter_time = 0; iter_time < maxInlineIter; iter_time++)
    {
        std::vector<Instruction *> need_pass;
        auto copy_func = deepCopy(func);
        for (auto bb : func->getBlockList())
        {
            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
            {
                if (inst->isCall() && dynamic_cast<FuncCallInstruction *>(inst)->getFuncSe()->getFunction() == func)
                {
                    need_pass.push_back(inst);
                }
            }
        }
        if (need_pass.size() < maxRecurCall)
        {
            for (auto inst : need_pass)
            {
                pass(inst, copy_func);
            }
        }
        delete copy_func;
    }
}

Function *AutoInliner::deepCopy(Function *old_func)
{
    std::map<Operand *, Operand *> ope2ope;
    for (auto fparam : old_func->getParamsOp())
    {
        ope2ope[fparam] = fparam;
    }
    for (auto id_se : unit->getDeclList())
    {
        if (id_se->getType()->isARRAY() || id_se->getType()->isInt() || id_se->getType()->isFloat())
        {
            ope2ope[id_se->getAddr()] = id_se->getAddr();
        }
    }

    std::map<BasicBlock *, BasicBlock *> bb2bb;
    std::map<PhiInstruction *, PhiInstruction *> phi2oldphi;
    Function *new_func = new Function(old_func);
    for (auto old_bb : old_func->getBlockList())
    {
        auto new_bb = new BasicBlock(new_func);
        bb2bb[old_bb] = new_bb;
        for (auto old_inst = old_bb->begin(); old_inst != old_bb->end(); old_inst = old_inst->getNext())
        {
            auto new_inst = old_inst->copy();
            new_inst->setParent(new_bb);
            new_bb->insertBack(new_inst);

            if (!new_inst->hasNoDef())
            {
                auto def = new_inst->getDef();
                Operand *new_def;
                if (ope2ope.count(def))
                {
                    new_def = ope2ope[def];
                }
                else
                {
                    new_def = copyOperand(def);
                    ope2ope[def] = new_def;
                }
                new_inst->setDef(new_def);
            }
            auto uses = new_inst->getUses();
            int use_idx = 0;
            for (auto use : uses)
            {
                Operand *new_use;
                if (ope2ope.count(use))
                {
                    new_use = ope2ope[use];
                }
                else
                {
                    new_use = copyOperand(use);
                    ope2ope[use] = new_use;
                }
                ReplSingleUseWith(new_inst, use_idx, ope2ope[use]);
                use_idx++;
            }

            if (new_inst->isPHI())
            {
                phi2oldphi[dynamic_cast<PhiInstruction *>(new_inst)] = dynamic_cast<PhiInstruction *>(old_inst);
            }
        }
    }

    new_func->setEntry(bb2bb[old_func->getEntry()]);
    for (auto bb : old_func->getBlockList())
    {
        auto newBlock = bb2bb[bb];
        for (auto it = bb->pred_begin(); it != bb->pred_end(); it++)
        {
            assert(bb2bb.count(*it));
            newBlock->addPred(bb2bb[*it]);
        }
        for (auto it = bb->succ_begin(); it != bb->succ_end(); it++)
        {
            assert(bb2bb.count(*it));
            newBlock->addSucc(bb2bb[*it]);
        }
    }

    for (auto bb : new_func->getBlockList())
    {
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            switch (inst->getInstType())
            {
            case Instruction::PHI:
            {
                auto new_phi = dynamic_cast<PhiInstruction *>(inst);
                assert(phi2oldphi.count(new_phi));
                auto old_srcs = phi2oldphi[new_phi]->getSrcs();
                for (auto [old_bb, old_op] : old_srcs)
                {
                    new_phi->getSrcs().erase(old_bb);
                    new_phi->getSrcs()[bb2bb[old_bb]] = ope2ope[old_op];
                }
                break;
            }
            case Instruction::COND:
            {
                auto cond = dynamic_cast<CondBrInstruction *>(inst);
                cond->setTrueBranch(bb2bb[cond->getTrueBranch()]);
                cond->setFalseBranch(bb2bb[cond->getFalseBranch()]);
                break;
            }
            case Instruction::UNCOND:
            {
                auto uncond = dynamic_cast<UncondBrInstruction *>(inst);
                uncond->setBranch(bb2bb[uncond->getBranch()]);
                break;
            }
            default:
                break;
            }
        }
    }

    return new_func;
}

void AutoInliner::RecurDetect()
{
    is_recur.clear();
    for (auto f : unit->getFuncList())
        is_recur[f] = false;
    for (auto cur_f : unit->getFuncList())
    {
        if (is_recur[cur_f])
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
            is_recur[f_nxt] = true;
            return;
        }
        else
        {
            Path.insert(f_nxt);
            UpdateRecur(f_nxt, Path);
            Path.erase(f_nxt);
        }
}