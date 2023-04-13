#include "DeadInstrElimanation.h"
#include <queue>

// 清除后继节点
void BasicBlock::CleanSucc() 
{
    for (auto i : succ) {
        i->removePred(this);
    }
    std::set<BasicBlock*>().swap(succ);
}

// 得到指令的数量
int BasicBlock::getNumofInstr()
{
    int num = 0;
    auto x = head;
    while (x->getNext() != head) {
        x = x->getNext();
        num ++;
    }
    return num;
}

// 清理DCE的标记
void BasicBlock::clearDCEMark() { DCE_marked = false; };
// 设置DCE标记
void BasicBlock::SetBDCEMark() { DCE_marked = true; };
// 判断是否有DCE标记
bool BasicBlock::isDCEMarked() { return DCE_marked; };
// 获得反向的严格支配着
std::set<BasicBlock*> &BasicBlock::getRSDoms() { return RSDoms; };
// 获得反向的立即支配者
BasicBlock* &BasicBlock::getRIdom() { return RiDom; };
// 获得反向的支配者边界
std::set<BasicBlock*> &BasicBlock::getRDF() { return RDF; };
// 清理DCE的标记
void Instruction::clearDCEMark() { DCE_marked = false; }; 
// 设置DCE的标记
void Instruction::SetDCEMark() { DCE_marked = true; };
// 判断是否存在DCE的标记
bool Instruction::isDCEMarked() { return DCE_marked; };
// 判断是否是关键函数
bool Function::isCritical()
{
    if (iscritical != -1) return iscritical;

    for (auto p_type : ((FunctionType*)sym_ptr->getType())->getParamsType()) {
        if (p_type->isARRAY())
            return iscritical = 1;
    }
    for (auto bb : block_list) {
        for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            if (instr->isCall()) {
                IdentifierSymbolEntry* funcSE =
                    (IdentifierSymbolEntry*)(((FuncCallInstruction*)instr)
                                                 ->getFuncSe());
                if (funcSE->isLibFunc()) {
                    iscritical = 1;
                    return iscritical;
                } else {
                    auto func = funcSE->get_function();
                    if (func == this)
                        continue;
                    if (func->isCritical() == 1) {
                        iscritical = 1;
                        return iscritical;
                    }
                }
            }
            else
            {
                if (!instr->HasNoDef()) {
                    auto def = instr->getDef();
                    if (def != nullptr && def->getEntry()->isVariable()) {
                        auto se = ((IdentifierSymbolEntry*)def->getEntry());
                        if (se->isGlobal())
                            return iscritical = 1;
                    }
                }
                for (auto use : instr->getUses()) {
                    if (use != nullptr && use->getEntry()->isVariable()) {
                        auto se = (IdentifierSymbolEntry*) use->getEntry();
                        if (se->isGlobal())
                            return iscritical = 1;
                    }
                }
            }
    }
    return iscritical = 0;
}
// 判断指令是否为关键指令
bool Instruction::isCritical()
{
    if (isRet()) 
    {
        if (getUses().empty()) return true;
        auto preds = parent->getParent()->getPreds();
        if (preds.empty())
            return true;
        
        // 只要有接收ret值的就要返回true
        for (auto it : preds)
            for (auto in : it.second)
                if (in->getDef()->usersNum())
                    return true;
        return false;
    }

    if (isCall())
    {
        IdentifierSymbolEntry* funcSE = (IdentifierSymbolEntry*)(((FuncCallInstruction*)this)->getFuncSe());
        if (funcSE->isLibFunc() 
         || funcSE->get_function()->isCritical()
        )
            return true;
    }

    return /*isUncond() || */isStore();
}

void Function::removePred(Instruction* instr)
{
    assert(instr->isCall());

    Function* func = instr->getParent()->getParent();
    if (preds_instr[func].count(instr))
        preds_instr[func].erase(instr);
}

void DeadInstrElimination::pass()
{
    auto fs = unit->get_fun_list();
    for (auto f : fs)
        pass(f);
}
// 计算函数所有的Ret指令并且当作出口
std::set<BasicBlock*>& Function::getExit()
{
    if (Exit.size() != 0) return Exit;
    for (auto b : block_list) 
        if (b->rbegin()->isRet())
            Exit.insert(b);
    // assert(Exit.size() == 2);
    return Exit;
}
// 计算反向严格支配者
void Function::ComputeRDom()
{
    SimplifyCFG sc(this->getParent());
    sc.pass(this);
    for (auto bb : getBlockList())
        bb->getRSDoms() = std::set<BasicBlock *>();
    std::set<BasicBlock *> all_bbs(getBlockList().begin(), getBlockList().end());
    for (auto removed_bb : getBlockList())
    {
        std::set<BasicBlock *> visited;
        std::queue<BasicBlock *> q;
        std::map<BasicBlock *, bool> is_visited;
        for (auto bb : getBlockList())
            is_visited[bb] = false;
        for (auto b : getExit())
            if (b != removed_bb) 
            {
                visited.insert(b);
                is_visited[b] = true;
                q.push(b);
            }
        while (!q.empty())
        {
            BasicBlock *cur = q.front();
            q.pop();
            for (auto pred = cur->pred_begin(); pred != cur->pred_end(); pred++) {
                if (*pred != removed_bb && !is_visited[*pred])
                {
                    q.push(*pred);
                    visited.insert(*pred);
                    is_visited[*pred] = true;
                }
            }
        }
        std::set<BasicBlock *> not_visited;
        set_difference(all_bbs.begin(), all_bbs.end(), visited.begin(), visited.end(), inserter(not_visited, not_visited.end()));
        for (auto bb : not_visited)
        {
            if (bb != removed_bb)
                bb->getRSDoms().insert(removed_bb); // strictly dominators
        }
    }
}
// 计算反向立即支配者
void Function::ComputeRiDom()
{
    std::set<BasicBlock *> temp_RIDoms;
    for (auto bb : getBlockList())
    {
        temp_RIDoms = bb->getRSDoms();
        for (auto rsdom : bb->getRSDoms())
        {
            std::set<BasicBlock *> diff_set;
            set_difference(temp_RIDoms.begin(), temp_RIDoms.end(), rsdom->getRSDoms().begin(), rsdom->getRSDoms().end(), inserter(diff_set, diff_set.end()));
            temp_RIDoms = diff_set;
        }
        if (!getExit().count(bb) && temp_RIDoms.size())
            bb->getRIdom() = *temp_RIDoms.begin();
    }
    // for (auto bb : getBlockList())
    //     if (!getExit().count(bb))
    //         fprintf(stderr, "IDom[B%d] = B%d\n", bb->getNo(), bb->getRIdom()->getNo());
}
// 计算反向支配边界
void Function::ComputeRDF()
{
    for (auto bb : getBlockList())
        bb->getRDF() = std::set<BasicBlock *>();
    // for (auto &u : block_list)
    // {
    //     for (auto v = u->pred_begin(); v != u->pred_end(); v++)
    //     {
    //         BasicBlock *p = u;
    //         while (!(p != *v && (*v)->getRSDoms().count(p)))
    //         {
    //             p->getRDF().insert(*v);
    //             p = p->getRIdom();
    //         }
    //     }
    // }
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : getBlockList())
        is_visited[bb] = false;
    std::queue<BasicBlock *> q;
    for (auto e : getExit()) {
        q.push(e);
        is_visited[e] = true;
    }
    while (!q.empty())
    {
        auto a = q.front();
        q.pop();
        std::vector<BasicBlock *> preds(a->pred_begin(), a->pred_end());
        for (auto b : preds)
        {
            auto x = a;
            while (b->getRSDoms().size() && !b->getRSDoms().count(x))
            {
                assert(!getExit().count(x));
                x->getRDF().insert(b);
                x = x->getRIdom();
            }
            if (!is_visited[b])
            {
                is_visited[b] = true;
                q.push(b);
            }
        }
    }
    for (auto bb : getBlockList())
        if (!getExit().count(bb))
            for (auto e : bb->getRDF())
                fprintf(stderr, "RDF[B%d] = B%d\n", bb->getNo(), e->getNo());
}
// 获得最近的反向支配者
BasicBlock *Function::get_nearest_dom(Instruction *instr)
{
    BasicBlock *bb = instr->getParent();
    auto nmp = bb->getRIdom();
    while (nmp != nullptr)
    {
        if (nmp->isDCEMarked())
            return nmp;
        nmp = nmp->getRIdom();
    }
    return nullptr;
}
// 死的指令的标记
void DeadInstrElimination::DeadInstrMark(Function *f)
{
    std::vector<Instruction *> WorkList;
    for (auto bb = f->begin(); bb != f->end(); bb++)
    {
        (*bb)->clearDCEMark();
        if ((*bb)->empty())
            continue;
        for (auto instr = (*bb)->begin(); instr != (*bb)->end(); instr = instr->getNext())
        {
            instr->clearDCEMark();
            if (instr->isCritical())
            {
                fprintf(stderr, "Mark B%d instrType%d\n", instr->getParent()->getNo(), instr->getInstType());
                instr->SetDCEMark();
                auto bb = instr->getParent();
                bb->SetBDCEMark();
                WorkList.push_back(instr);
            }
        }
    }

    f->ComputeRDom();
    f->ComputeRiDom();
    f->ComputeRDF();

    while (!WorkList.empty())
    {
        auto instr = WorkList.back();
        WorkList.pop_back();
        if (!instr->HasNoUse()) {
            std::vector<Operand *> op_ptrs = instr->getUses();
            for (auto &op_ptr : op_ptrs)
            {
                Instruction *op_def = op_ptr->getDef();
                if (op_def && !op_def->isDCEMarked())
                {
                    fprintf(stderr, "UseMark B%d instrType%d\n", op_def->getParent()->getNo(), op_def->getInstType());
                    op_def->SetDCEMark();
                    auto bb = op_def->getParent();
                    bb->SetBDCEMark();
                    WorkList.push_back(op_def);
                }
            }
        }

        if (!instr->HasNoDef()) {
            auto def = instr->getDef();
            for (auto use = def->use_begin(); use != def->use_end(); use++) {
                if (!(*use)->isDCEMarked() &&
                    ((*use)->isUncond() || (*use)->isCond())) {
                    fprintf(stderr, "UseMark B%d instrType%d\n", (*use)->getParent()->getNo(), (*use)->getInstType());
                    (*use)->SetDCEMark();
                    (*use)->getParent()->SetBDCEMark();
                    WorkList.push_back(*use);
                }
            }
        }
        BasicBlock *bb = instr->getParent();
        if (!bb) continue;
        for (auto bbrdf : bb->getRDF())
        {
            auto instr_br = bbrdf->rbegin();
            if (instr_br != nullptr && (instr_br->isCond() || instr_br->isUncond()) && !instr_br->isDCEMarked())
            {
                fprintf(stderr, "RDFMark B%d instrType%d\n", instr_br->getParent()->getNo(), instr_br->getInstType());
                instr_br->SetDCEMark();
                WorkList.push_back(instr_br);
            }
        }
        for (auto in = bb->begin(); in != bb->end(); in = in->getNext()) {
            if (!in->isPHI())
                continue;
            auto phi = (PhiInstruction*)in;
            for (auto it : phi->getSrcs()) {
                Instruction* in = it.first->rbegin();
                fprintf(stderr, "retain B%d\n", it.first->getNo());
                if (!in->isDCEMarked() && (in->isCond() || in->isUncond())) {
                    // fprintf(stderr, "phiMark B%d instrType%d\n", in->getParent()->getNo(), in->getInstType());
                    in->SetDCEMark();
                    in->getParent()->SetBDCEMark();
                    WorkList.push_back(in);
                }
            }
        }
    }
}
// 死的指令的清除
void DeadInstrElimination::DeadInstrEliminate(Function *f)
{
    std::vector<Instruction *> Target;

    // for (auto bb = f->begin(); bb != f->end(); bb++) {
    //     if ((*bb)->empty())
    //         continue;
    //     for (auto instr = (*bb)->begin(); instr != (*bb)->end(); instr = instr->getNext())
    //         if (!instr->isDCEMarked()) {
    //             fprintf(stderr, "Block[%d] will be remove instruction %d!\n", instr->getParent()->getNo(), instr->getInstType());
    //         }
    // }
    for (auto bb = f->begin(); bb != f->end(); bb++)
    {
        if ((*bb)->empty())
            continue;
        for (auto instr = (*bb)->begin(); instr != (*bb)->end(); instr = instr->getNext())
            if (!instr->isDCEMarked()) {
                if (instr->isRet()) {
                    // assert(0);
                    instr->getUses()[0] = (
                        new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0)));
                }
                if (instr->isCall()) {
                    if (instr->isCritical()) continue;
                    IdentifierSymbolEntry* funcSE =
                            (IdentifierSymbolEntry*)(((FuncCallInstruction*)instr)
                                                         ->getFuncSe());
                    if (!funcSE->isLibFunc())
                        funcSE->get_function()->removePred(instr);
                }
                if (!instr->isUncond())
                    Target.push_back(instr);
                if (instr->isCond())
                {
                    BasicBlock *npd = f->get_nearest_dom(instr);
                    if (npd == nullptr) return;
                    new UncondBrInstruction(npd, *bb);
                    (*bb)->CleanSucc();
                    (*bb)->addSucc(npd);
                    npd->addPred(*bb);
                }
            }
    }

    for (auto ta : Target)
    {
        /*Ret problem*/
        // assert(ta->isRet());
        fprintf(stderr, "Block[%d] will remove instruction %d!\n", ta->getParent()->getNo(), ta->getInstType());
        auto p = ta->getParent();
        p->remove(ta);
    }
}
// 删除所有没有前导的非入口点
void DeadInstrElimination::DeleteBbWithNoPred(Function* f) 
{
    while (true) {
        std::vector<BasicBlock*> temp;
        for (auto block : f->getBlockList())
            if (block->getNumOfPred() == 0 && block != f->getEntry())
                temp.push_back(block);
        for (auto block : temp) {
            block->CleanSucc();
            delete block;
        }
        if (temp.size() == 0)
            break;
    }
}

void DeadInstrElimination::pass(Function *f)
{
    DeleteBbWithNoPred(f);
    DeadInstrMark(f);
    DeadInstrEliminate(f);
    DeleteBbWithNoPred(f);
    SimplifyCFG sc(f->getParent());
    sc.pass();
}