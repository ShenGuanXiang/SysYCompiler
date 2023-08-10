#include "DeadCodeElim.h"
#include "LiveVariableAnalysis.h"
#include <vector>
#include <map>
#include <set>
#include <queue>

// TODO:删除@开头的汇编语句、arm汇编删除无用cmp

// 清除后继节点
void BasicBlock::CleanSucc()
{
    for (auto i : succ)
    {
        i->removePred(this);
    }
    std::set<BasicBlock *>().swap(succ);
}

// 得到指令的数量
int BasicBlock::getNumofInstr()
{
    int num = 0;
    auto x = head;
    while (x->getNext() != head)
    {
        x = x->getNext();
        num++;
    }
    return num;
}

// 获得反向的严格支配着
std::set<BasicBlock *> &BasicBlock::getRSDoms() { return RSDoms; };
// 获得反向的立即支配者
BasicBlock *&BasicBlock::getRIdom() { return RiDom; };
// 获得反向的支配者边界
std::set<BasicBlock *> &BasicBlock::getRDF() { return RDF; };

// 判断是否是关键函数
bool Function::isCritical()
{
    if (iscritical != -1)
        return iscritical;

    for (auto p_type : ((FunctionType *)sym_ptr->getType())->getParamsType())
    {
        if (p_type->isARRAY())
            return iscritical = 1;
    }
    for (auto bb : block_list)
    {
        for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
            if (instr->isCall())
            {
                IdentifierSymbolEntry *funcSE = (IdentifierSymbolEntry *)(((FuncCallInstruction *)instr)->getFuncSe());
                if (funcSE->isLibFunc())
                    return iscritical = 1;
                else
                {
                    auto func = funcSE->getFunction();
                    if (func == this)
                        continue;
                    if (func->isCritical() == 1)
                        return iscritical = 1;
                }
            }
            else
            {
                if (!instr->hasNoDef())
                {
                    auto def = instr->getDef();
                    if (def != nullptr && def->getEntry()->isVariable())
                    {
                        auto se = ((IdentifierSymbolEntry *)def->getEntry());
                        if (se->isGlobal())
                            return iscritical = 1;
                    }
                }
                for (auto use : instr->getUses())
                {
                    if (use != nullptr && use->getEntry()->isVariable())
                    {
                        auto se = (IdentifierSymbolEntry *)use->getEntry();
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
        if (getUses().empty())
            return true;
        auto preds = parent->getParent()->getCallersInsts();
        if (!preds.empty() || ((IdentifierSymbolEntry *)parent->getParent()->getSymPtr())->isMain())
            return true;

        // 只要有接收ret值的就要返回true
        for (auto in : preds)
            if (in->getDef()->usersNum())
                return true;
        return false;
    }

    if (isCall())
    {
        IdentifierSymbolEntry *funcSE = (IdentifierSymbolEntry *)(((FuncCallInstruction *)this)->getFuncSe());
        if (funcSE->isLibFunc() || funcSE->getFunction()->isCritical())
            return true;
    }

    return /*isUncond() || */ isStore();
}

void DeadCodeElim::pass()
{
    SimplifyCFG sc(unit);
    sc.pass();

    auto fs = unit->getFuncList();
    for (auto f : fs)
        pass(f);

    deleteUselessFunc();

    sc.pass();
}
// 计算函数所有的Ret指令并且当作出口
std::set<BasicBlock *> Function::getExits()
{
    std::set<BasicBlock *> exits;
    for (auto b : block_list)
        if (b->rbegin()->isRet())
            exits.insert(b);
    return exits;
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
        auto exits = getExits();
        for (auto b : exits)
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
            for (auto pred = cur->pred_begin(); pred != cur->pred_end(); pred++)
            {
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
    auto exits = getExits();
    for (auto bb : getBlockList())
    {
        std::set<BasicBlock *> temp_RIDoms(bb->getRSDoms());
        for (auto rsdom : bb->getRSDoms())
        {
            std::set<BasicBlock *> diff_set;
            set_difference(temp_RIDoms.begin(), temp_RIDoms.end(), rsdom->getRSDoms().begin(), rsdom->getRSDoms().end(), inserter(diff_set, diff_set.end()));
            temp_RIDoms = diff_set;
        }
        if (!exits.count(bb) && temp_RIDoms.size())
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
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : getBlockList())
        is_visited[bb] = false;
    std::queue<BasicBlock *> q;
    auto exits = getExits();
    for (auto e : exits)
    {
        q.push(e);
        is_visited[e] = true;
    }
    while (!q.empty())
    {
        auto a = q.front();
        q.pop();
        std::set<BasicBlock *> preds(a->pred_begin(), a->pred_end());
        for (auto b : preds)
        {
            auto x = a;
            while (b->getRSDoms().size() && !b->getRSDoms().count(x))
            {
                assert(!exits.count(x));
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
    // for (auto bb : getBlockList())
    //     if (!getExit().count(bb))
    //         for (auto e : bb->getRDF())
    //             fprintf(stderr, "RDF[B%d] = B%d\n", bb->getNo(), e->getNo());
}
// 获得最近的反向支配者
BasicBlock *DeadCodeElim::get_nearest_dom(Instruction *instr)
{
    BasicBlock *bb = instr->getParent();
    auto nmp = bb->getRIdom();
    while (nmp != nullptr)
    {
        if (bbDCEMarked[nmp])
            return nmp;
        nmp = nmp->getRIdom();
    }
    return nullptr;
}
// 死的指令的标记
void DeadCodeElim::deadInstrMark(Function *f)
{
    instDCEMarked.clear();
    bbDCEMarked.clear();
    std::vector<Instruction *> worklist;
    for (auto bb = f->begin(); bb != f->end(); bb++)
    {
        if (*bb != f->getEntry() && (*bb)->predEmpty())
            continue;
        bbDCEMarked[*bb] = false;
        if ((*bb)->empty())
            continue;
        for (auto instr = (*bb)->begin(); instr != (*bb)->end(); instr = instr->getNext())
        {
            instDCEMarked[instr] = false;
            if (instr->isCritical())
            {
                // fprintf(stderr, "Mark B%d instrType%d\n", instr->getParent()->getNo(), instr->getInstType());
                instDCEMarked[instr] = true;
                bbDCEMarked[instr->getParent()] = true;
                worklist.push_back(instr);
            }
        }
    }
    f->ComputeRDom();
    f->ComputeRiDom();
    f->ComputeRDF();
    while (!worklist.empty())
    {
        auto instr = worklist.back();
        worklist.pop_back();
        if (!instr->hasNoUse())
        {
            std::vector<Operand *> op_ptrs = instr->getUses();
            for (auto &op_ptr : op_ptrs)
            {
                Instruction *op_def = op_ptr->getDef();
                if (op_def && !instDCEMarked[op_def])
                {
                    // fprintf(stderr, "UseMark B%d instrType%d\n", op_def->getParent()->getNo(), op_def->getInstType());
                    instDCEMarked[op_def] = true;
                    bbDCEMarked[op_def->getParent()] = true;
                    worklist.push_back(op_def);
                }
            }
        }

        if (!instr->hasNoDef())
        {
            auto def = instr->getDef();
            for (auto use = def->use_begin(); use != def->use_end(); use++)
            {
                if (!instDCEMarked[*use] && ((*use)->isUncond() || (*use)->isCond()))
                {
                    // fprintf(stderr, "UseMark B%d instrType%d\n", (*use)->getParent()->getNo(), (*use)->getInstType());
                    instDCEMarked[*use] = true;
                    bbDCEMarked[(*use)->getParent()] = true;
                    worklist.push_back(*use);
                }
            }
        }
        BasicBlock *bb = instr->getParent();
        if (!bb)
            continue;
        for (auto bb_rdf : bb->getRDF())
        {
            auto instr_br = bb_rdf->rbegin();
            if (instr_br != nullptr && (instr_br->isCond() || instr_br->isUncond()) && !instDCEMarked[instr_br])
            {
                // fprintf(stderr, "RDFMark B%d instrType%d\n", instr_br->getParent()->getNo(), instr_br->getInstType());
                instDCEMarked[instr_br] = true;
                worklist.push_back(instr_br);
            }
        }
        for (auto in = bb->begin(); in != bb->end(); in = in->getNext())
        {
            if (!in->isPHI())
                continue;
            auto phi = (PhiInstruction *)in;
            for (auto it : phi->getSrcs())
            {
                Instruction *in = it.first->rbegin();
                // fprintf(stderr, "retain B%d\n", it.first->getNo());
                if (!instDCEMarked[in] && (in->isCond() || in->isUncond()))
                {
                    // fprintf(stderr, "phiMark B%d instrType%d\n", in->getParent()->getNo(), in->getInstType());
                    instDCEMarked[in] = true;
                    bbDCEMarked[in->getParent()] = true;
                    worklist.push_back(in);
                }
            }
        }
    }
}
// 死的指令的清除
void DeadCodeElim::deadInstrEliminate(Function *f)
{
    std::vector<Instruction *> Target;
    for (auto bb = f->begin(); bb != f->end(); bb++)
    {
        if ((*bb)->empty())
            continue;
        for (auto instr = (*bb)->begin(); instr != (*bb)->end(); instr = instr->getNext())
            if (!instDCEMarked[instr])
            {
                if (instr->isRet())
                {
                    // assert(0);
                    instr->getUses()[0] = (new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0)));
                }
                if (!instr->isUncond())
                    Target.push_back(instr);
                if (instr->isCond())
                {
                    BasicBlock *npd = get_nearest_dom(instr);
                    if (npd == nullptr)
                        return;
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
        // fprintf(stderr, "Block[%d] will remove instruction %d!\n", ta->getParent()->getNo(), ta->getInstType());

        // auto p = ta->getParent();
        // p->remove(ta);
        delete ta;
    }
}

void DeadCodeElim::pass(Function *f)
{
    deadInstrMark(f);
    deadInstrEliminate(f);
}

void DeadCodeElim::deleteUselessFunc()
{
    std::map<Function *, bool> ever_called;
    for (auto f : unit->getFuncList())
        ever_called[f] = false;
    Function *main_func = unit->getMainFunc();
    std::queue<Function *> called_funcs;
    called_funcs.push(main_func);
    ever_called[main_func] = true;
    while (!called_funcs.empty())
    {
        auto t = called_funcs.front();
        called_funcs.pop();
        for (auto bb : t->getBlockList())
            for (auto instr = bb->begin(); instr != bb->end(); instr = instr->getNext())
                if (instr->isCall() &&
                    !((IdentifierSymbolEntry *)((FuncCallInstruction *)instr)->getFuncSe())->isLibFunc())
                {
                    auto called_func = ((IdentifierSymbolEntry *)((FuncCallInstruction *)instr)->getFuncSe())->getFunction();
                    if (!called_func || ever_called[called_func] == true)
                        continue;
                    ever_called[called_func] = true;
                    called_funcs.push(called_func);
                }
    }
    for (auto f : unit->getFuncList())
        if (!ever_called[f] && !((IdentifierSymbolEntry *)f->getSymPtr())->isMain())
            delete f;
}

void MachineDeadCodeElim::pass(bool iter)
{
    for (auto &f : unit->getFuncs())
    {
        pass(f, iter);
        // SingleBrDelete(f);
    }
}

MachineInstruction *MachineBlock::getNext(MachineInstruction *instr)
{
    auto it = find(inst_list.begin(), inst_list.end(), instr);
    if (it != inst_list.end() && (it + 1) != inst_list.end())
    {
        return *(it + 1);
    }
    return nullptr;
}

void MachineDeadCodeElim::pass(MachineFunction *f, bool iter)
{
    bool change;
    do
    {
        change = false;
        f->AnalyzeLiveVariable();
        std::map<MachineOperand, std::set<MachineOperand *>> out;
        std::vector<MachineInstruction *> deleteList;
        for (auto b : f->getBlocks())
        {
            out.clear();

            for (auto liveout : b->getLiveOut())
                out[*liveout].insert(liveout);

            auto &Insts = b->getInsts();
            for (auto itr = Insts.rbegin(); itr != Insts.rend(); itr++)
            {
                auto defs = (*itr)->getDef();
                if (!defs.empty())
                {
                    MachineOperand *def = nullptr;
                    def = defs[0];
                    if (out[*def].empty() && defs.size() == 1)
                        deleteList.push_back(*itr);
                }

                for (auto &def : defs)
                {
                    auto &O = f->getAllUses()[*def];
                    for (auto &o : O)
                        if (out[*def].find(o) != out[*def].end())
                            out[*def].erase(o);
                }
                auto uses = (*itr)->getUse();
                for (auto &use : uses)
                    out[*use].insert(use);
            }
        }
        for (auto t : deleteList)
        {
            if (t->isCritical())
                continue;
            if (t->isCondMov())
            {
                // auto next = t->getParent()->getNext(t);
                // if (next && next->isCondMov())
                continue;
            }
            if (t != nullptr)
            {
                change = true;
                delete t;
            }
        }
    } while (change && iter);
}

void MachineDeadCodeElim::SingleBrDelete(MachineFunction *f)
{
    std::vector<MachineBlock *> delete_bbs;
    for (auto &bb : f->getBlocks())
    {
        if (bb->getInsts().size() != 1)
            continue;
        MachineInstruction *inst = *bb->getInsts().begin();
        if (!inst->isBranch())
            continue;
        BranchMInstruction *br = dynamic_cast<BranchMInstruction *>(inst);
        delete_bbs.push_back(bb);
        for (auto &pred : bb->getPreds())
        {
            pred->addSucc(bb->getSuccs()[0]);
            bb->getSuccs()[0]->addPred(pred);
            if (pred->getInsts().empty())
                continue;
            MachineInstruction *inst1 = pred->getInsts().back();
            if (!inst1->isBranch())
                continue;
            BranchMInstruction *br1 = dynamic_cast<BranchMInstruction *>(inst1);
            if (br1->getParent()->getNo() == bb->getNo())
            {
                br1->setParent(br->getParent());
            }
            if (pred->getInsts().size() <= 1)
                continue;
            auto it = pred->getInsts().rbegin();
            it++;
            inst1 = *it;
            if (!inst1->isBranch())
                continue;
            br1 = dynamic_cast<BranchMInstruction *>(inst1);
            if (br1->getParent()->getNo() == bb->getNo())
            {
                br1->setParent(br->getParent());
            }
        }
    }

    for (auto bb : delete_bbs)
    {
        delete bb;
    }
}