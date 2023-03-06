#include "SimplifyCFG.h"
#include "Type.h"
#include <queue>
#include <set>

std::set<BasicBlock *> freeBBs;
std::set<Instruction *> freeInsts;

void SimplifyCFG::pass()
{
    auto Funcs = std::vector<Function *>(unit->begin(), unit->end());
    for (auto func : Funcs)
        pass(func);
}

void SimplifyCFG::pass(Function *func)
{
    // bfs
    std::map<BasicBlock *, bool> is_visited;
    for (auto bb : func->getBlockList())
        is_visited[bb] = false;
    std::queue<BasicBlock *> q;
    q.push(func->getEntry());
    is_visited[func->getEntry()] = true;
    while (!q.empty())
    {
        auto bb = q.front();
        std::vector<BasicBlock *> preds(bb->pred_begin(), bb->pred_end());
        std::vector<BasicBlock *> succs(bb->succ_begin(), bb->succ_end());
        // 消除空的基本块，比如某些end_bb
        if (bb->empty() && bb != func->getEntry())
        {
            assert(bb->succEmpty());
            for (auto pred : preds)
            {
                auto lastInst = pred->rbegin();
                if (lastInst->isUncond())
                {
                    pred->remove(lastInst);
                    freeInsts.insert(lastInst);
                }
                else
                {
                    assert(lastInst->isCond());
                    pred->remove(lastInst);
                    freeInsts.insert(lastInst);
                    CondBrInstruction *branch = (CondBrInstruction *)(lastInst);
                    if (branch->getTrueBranch() == bb)
                        new UncondBrInstruction(branch->getFalseBranch(), pred);
                    else
                        new UncondBrInstruction(branch->getTrueBranch(), pred);
                }
                pred->removeSucc(bb);
            }
            func->remove(bb);
            freeBBs.insert(bb);
        }
        // 消除仅包含无条件分支的基本块。
        else if (bb->begin()->getNext() == bb->end() && bb->begin()->isUncond())
        {
            assert(bb->getNumOfSucc() == 1);
            if (bb == func->getEntry())
            {
                if (succs[0]->getNumOfPred() == 1)
                {
                    func->setEntry(succs[0]);
                    freeBBs.insert(bb);
                }
                goto Next;
            }
            bool eliminable = true;
            for (auto i = succs[0]->begin(); i != succs[0]->end() && i->isPHI(); i = i->getNext())
            {
                auto srcs = ((PhiInstruction *)i)->getSrcs();
                for (auto pred : preds)
                    if (srcs.count(pred))
                        eliminable = false;
            }
            if (eliminable)
            {
                succs[0]->removePred(bb);
                for (auto pred : preds)
                {
                    pred->removeSucc(bb);
                    auto lastInst = pred->rbegin();
                    if (lastInst->isCond())
                    {
                        CondBrInstruction *branch = (CondBrInstruction *)(lastInst);
                        if (branch->getTrueBranch() == bb)
                            branch->setTrueBranch(succs[0]);
                        else
                            branch->setFalseBranch(succs[0]);
                        if (branch->getTrueBranch() == branch->getFalseBranch())
                        {
                            pred->remove(lastInst);
                            freeInsts.insert(lastInst);
                            new UncondBrInstruction(branch->getTrueBranch(), pred);
                        }
                    }
                    else
                    {
                        assert(lastInst->isUncond());
                        pred->remove(lastInst);
                        freeInsts.insert(lastInst);
                        new UncondBrInstruction(succs[0], pred);
                    }
                    pred->addSucc(succs[0]);
                    succs[0]->addPred(pred);
                }
                // 更新PHI
                for (auto i = succs[0]->begin(); i != succs[0]->end() && i->isPHI(); i = i->getNext())
                {
                    auto &srcs = ((PhiInstruction *)i)->getSrcs();
                    assert(srcs.count(bb));
                    for (auto pred : preds)
                        // if (!srcs.count(pred))
                        srcs[pred] = srcs[bb];
                    srcs.erase(bb);
                }
                if (bb == func->getEntry())
                    func->setEntry(succs[0]);
                func->remove(bb);
                freeBBs.insert(bb);
            }
        }
        // 如果仅有一个前驱且该前驱仅有一个后继，将基本块与前驱合并
        else if (bb->getNumOfPred() == 1 && (*(bb->pred_begin()))->getNumOfSucc() == 1 && bb != func->getEntry())
        {
            auto pred = *(bb->pred_begin());
            pred->removeSucc(bb);
            auto lastInst = pred->rbegin();
            assert(lastInst->isUncond() || (lastInst->isCond() && ((CondBrInstruction *)(lastInst))->getTrueBranch() == ((CondBrInstruction *)(lastInst))->getFalseBranch()));
            pred->remove(lastInst);
            freeInsts.insert(lastInst);
            for (auto succ : succs)
                pred->addSucc(succ);
            auto insts = std::vector<Instruction *>();
            for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
            {
                assert(!inst->isPHI());
                insts.push_back(inst);
            }
            for (auto inst : insts)
            {
                bb->remove(inst);
                freeInsts.erase(inst);
                pred->insertBefore(inst, pred->end());
            }
            for (auto succ : succs)
            {
                // 更新PHI
                for (auto i = succ->begin(); i != succ->end() && i->isPHI(); i = i->getNext())
                {
                    auto &srcs = ((PhiInstruction *)i)->getSrcs();
                    assert(srcs.count(bb));
                    for (auto pred : preds)
                    {
                        assert(!srcs.count(pred));
                        srcs[pred] = srcs[bb];
                    }
                    srcs.erase(bb);
                }
                succ->removePred(bb);
                succ->addPred(pred);
            }
            func->remove(bb);
            freeBBs.insert(bb);
        }
    Next:
        q.pop();
        for (auto succ : succs)
        {
            if (!is_visited[succ])
            {
                q.push(succ);
                is_visited[succ] = true;
            }
        }
    }
    // 删除不可达的基本块。
    auto blocks = func->getBlockList();
    for (auto bb : blocks)
        if (!is_visited[bb])
        {
            func->remove(bb);
            std::vector<BasicBlock *> preds(bb->pred_begin(), bb->pred_end());
            std::vector<BasicBlock *> succs(bb->succ_begin(), bb->succ_end());
            for (auto pred : preds)
                pred->removeSucc(bb);
            for (auto succ : succs)
                succ->removePred(bb);
            freeBBs.insert(bb);
        }
    for (auto inst : freeInsts)
        delete inst;
    freeInsts.clear();
    for (auto bb : freeBBs)
        delete bb;
    freeBBs.clear();
}