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

// todo:某些情况下phi增加来源的代价反而比消除控制流更高，可以判断一下
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
            goto Next;
        }
        // 将两个目标基本块相同的条件分支削弱为无条件分支
        if (bb->rbegin()->isCond() && ((CondBrInstruction *)(bb->rbegin()))->getTrueBranch() == ((CondBrInstruction *)(bb->rbegin()))->getFalseBranch())
        {
            assert(0);
            // auto lastInst = bb->rbegin();
            // bb->remove(lastInst);
            // freeInsts.insert(lastInst);
            // new UncondBrInstruction(((CondBrInstruction *)lastInst)->getTrueBranch(), bb);
        }
        // 消除仅包含无条件跳转的基本块。
        if (bb->begin()->getNext() == bb->end() && bb->begin()->isUncond())
        {
            assert(bb->getNumOfSucc() == 1);
            if (bb == func->getEntry())
            {
                if (succs[0]->getNumOfPred() == 1)
                {
                    func->setEntry(succs[0]);
                    func->remove(bb);
                    freeBBs.insert(bb);
                }
                goto Next;
            }
            bool eliminable = true;
            for (auto i = succs[0]->begin(); i != succs[0]->end() && i->isPHI(); i = i->getNext())
            {
                auto srcs = ((PhiInstruction *)i)->getSrcs();
                for (auto pred : preds)
                    if (srcs.count(pred) && srcs[pred] != srcs[bb] && !(srcs[pred]->getType()->isConst() && srcs[bb]->getType()->isConst() && srcs[pred]->getEntry()->getValue() == srcs[bb]->getEntry()->getValue()))
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
        // 无条件跳转到只有一个条件跳转语句的基本块的情况可以被简化
        else if (bb->begin()->getNext() == bb->end() && bb->begin()->isCond())
        {
            assert(0);
            // auto condOpe = ((CondBrInstruction *)bb->begin())->getUses()[0];
            // auto trueBB = ((CondBrInstruction *)bb->begin())->getTrueBranch();
            // auto falseBB = ((CondBrInstruction *)bb->begin())->getFalseBranch();
            // for (auto pred : preds)
            //     if (pred->getNumOfSucc() == 1)
            //     {
            //         auto lastInst = pred->rbegin();
            //         assert(lastInst->isUncond());
            //         pred->removeSucc(bb);
            //         bb->removePred(pred);
            //         pred->remove(lastInst);
            //         freeInsts.insert(lastInst);
            //         new CondBrInstruction(trueBB, falseBB, condOpe, pred);
            //         assert(pred->begin()->getNext() != pred->end());
            //         // 更新PHI
            //         for (auto phi_bb : {falseBB, trueBB})
            //             for (auto i = phi_bb->begin(); i != phi_bb->end() && i->isPHI(); i = i->getNext())
            //             {
            //                 auto &srcs = ((PhiInstruction *)i)->getSrcs();
            //                 assert(srcs.count(bb));
            //                 for (auto pred : preds)
            //                 {
            //                     assert(!srcs.count(pred));
            //                     srcs[pred] = srcs[bb];
            //                 }
            //                 srcs.erase(bb);
            //             }
            //         trueBB->addPred(pred);
            //         falseBB->addPred(pred);
            //         pred->addSucc(trueBB);
            //         pred->addSucc(falseBB);
            //     }
            // if (bb->predEmpty())
            // {
            //     trueBB->removePred(bb);
            //     falseBB->removePred(bb);
            //     func->remove(bb);
            //     freeBBs.insert(bb);
            // }
        }
        // 无条件跳转到只有一个ret语句的基本块的情况也可以被简化
        else if (bb->begin()->getNext() == bb->end() && bb->begin()->isRet())
        {
            for (auto pred : preds)
                if (pred->getNumOfSucc() == 1)
                {
                    auto lastInst = pred->rbegin();
                    assert(lastInst->isUncond());
                    pred->removeSucc(bb);
                    bb->removePred(pred);
                    pred->remove(lastInst);
                    freeInsts.insert(lastInst);
                    new RetInstruction(bb->begin()->getUses().empty() ? nullptr : bb->begin()->getUses()[0], pred);
                    if (pred->begin()->getNext() == pred->end())
                    {
                        std::vector<BasicBlock *> pred_preds(pred->pred_begin(), pred->pred_end());
                        for (auto pred_pred : pred_preds)
                        {
                            pred_pred->removeSucc(pred);
                            auto lastInst = pred_pred->rbegin();
                            if (lastInst->isCond())
                            {
                                CondBrInstruction *branch = (CondBrInstruction *)(lastInst);
                                if (branch->getTrueBranch() == pred)
                                    branch->setTrueBranch(bb);
                                else
                                    branch->setFalseBranch(bb);
                                if (branch->getTrueBranch() == branch->getFalseBranch())
                                {
                                    pred_pred->remove(lastInst);
                                    freeInsts.insert(lastInst);
                                    new UncondBrInstruction(branch->getTrueBranch(), pred_pred);
                                }
                            }
                            else
                            {
                                assert(lastInst->isUncond());
                                pred_pred->remove(lastInst);
                                freeInsts.insert(lastInst);
                                new UncondBrInstruction(bb, pred_pred);
                            }
                            pred_pred->addSucc(bb);
                            bb->addPred(pred_pred);
                        }
                        func->remove(pred);
                        freeBBs.insert(pred);
                    }
                    if (bb->predEmpty())
                    {
                        func->remove(bb);
                        freeBBs.insert(bb);
                    }
                }
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