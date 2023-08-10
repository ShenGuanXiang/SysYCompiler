#include "Straighten.h"

void Straighten::getSlimBlock()
{
    // 得到每个只有一条b指令的基本块及其后继基本块
    for (auto &func : unit->getFuncs())
    {
        for (auto &blk : func->getBlocks())
        {
            if (blk->getInsts().size() == 1 && blk->getInsts()[0]->isBranch() && dynamic_cast<BranchMInstruction*>(blk->getInsts()[0])->getOpType() == BranchMInstruction::B)
            {
                // 先不处理entry块
                if (blk == func->getEntry())
                    continue;
                blk2blk[blk->getNo()] = std::make_pair(blk, blk->getSuccs()[0]);
            }
        }
    }
}

void Straighten::removeSlimBlock()
{
    std::string label;
    MachineBlock *direct_succ;
    MachineBlock *last_succ;
    bool is_direct_succ;
    int target_no;
    for (auto &pa : blk2blk)
    {
        auto prev = pa.second.first;
        auto succ = pa.second.second;
        prev->getParent()->removeBlock(prev);
        prev->removeSucc(succ);
        succ->removePred(prev);
    }
    for (auto &func : unit->getFuncs())
    {
        for (auto &blk : func->getBlocks())
        {
            for (auto &ins : blk->getInsts())
            {
                if (ins->isBranch())
                {
                    label = ins->getUse()[0]->getLabel();
                    fprintf(stderr, "label is %s\n", label.c_str());
                    label = label.substr(2, label.size() - 2);
                    target_no = atoi(label.c_str());
                    is_direct_succ = true;
                    while (blk2blk.find(target_no) != blk2blk.end())
                    {
                        if (is_direct_succ)
                        {
                            direct_succ = blk2blk[target_no].first;
                            is_direct_succ = false;
                        }
                        last_succ = blk2blk[target_no].second;
                        target_no = last_succ->getNo();
                    }
                    if (!is_direct_succ)
                    {
                        blk->removeSucc(direct_succ);
                        blk->addSucc(last_succ);
                        last_succ->addPred(blk);
                        ((BranchMInstruction *)ins)->setTarget(new MachineOperand(".L" + std::to_string(last_succ->getNo())));
                    }
                }
            }
        }
        auto entry = func->getEntry();
        if (entry->getInsts().size() == 1 && entry->getInsts()[0]->isBranch() && dynamic_cast<BranchMInstruction*>(entry->getInsts()[0])->getOpType() == BranchMInstruction::B)
        {
            auto succ = entry->getSuccs()[0];
            entry->removeSucc(succ);
            succ->removePred(entry);
            func->removeBlock(entry);
            func->removeBlock(succ);
            func->insertFront(succ);
            func->setEntry(succ);
        }
    }
}

void Straighten::getJunctions()
{
    for (auto &func : unit->getFuncs())
    {
        for (auto &blk : func->getBlocks())
        {
            if (blk->getPreds().size() == 1 && blk->getPreds()[0]->getSuccs().size() == 1)
            {
                assert((*blk->getPreds()[0]->rbegin())->isBranch() && "最后一条指令应该是无条件跳转");
                junctions.insert(blk);
            }
        }
    }
}

void Straighten::mergeJunctions()
{
    std::set<MachineBlock *> color;
    for (auto &blk : junctions)
    {
        if (color.count(blk))
            continue;
        auto headBlk = blk;
        while (junctions.count(headBlk))
            headBlk = headBlk->getPreds()[0];
        auto junctionBlk = headBlk->getSuccs()[0];
        while (junctions.count(junctionBlk))
        {
            color.insert(junctionBlk);
            auto lastBranch = *headBlk->rbegin();
            headBlk->removeInst(lastBranch);
            for (auto &ins : junctionBlk->getInsts())
            {
                headBlk->removeInst(ins);
                ins->setParent(headBlk);
            }
            headBlk->getSuccs().clear();
            headBlk->getSuccs().assign(junctionBlk->getSuccs().begin(), junctionBlk->getSuccs().end());
            for (auto &succs : headBlk->getSuccs())
            {
                succs->removePred(junctionBlk);
                succs->addPred(headBlk);
            }
            junctionBlk->getParent()->removeBlock(junctionBlk);
            junctionBlk = junctionBlk->getSuccs().size() > 0 ? junctionBlk->getSuccs()[0] : nullptr;
        }
    }
}

void Straighten::pass()
{
    blk2blk.clear();
    junctions.clear();
#ifdef PRINTLOG
    Log("伸直化开始");
#endif
    getSlimBlock();
    removeSlimBlock();
#ifdef PRINTLOG
    Log("伸直化阶段1完成");
#endif
    getJunctions();
    mergeJunctions();
#ifdef PRINTLOG
    Log("伸直化完成\n");
#endif
}

void Straighten::pass2()
{
    for (auto func : unit->getFuncs())
    {
        std::map<int, MachineBlock *> no2blk;
        for (auto blk : func->getBlocks())
            no2blk[blk->getNo()] = blk;
        std::vector<MachineBlock *> tmp_blks;
        tmp_blks.assign(func->getBlocks().begin(), func->getBlocks().end());
        func->getBlocks().clear();
        for (auto i = 0; i < tmp_blks.size(); i++)
        {
            if (no2blk.find(tmp_blks[i]->getNo()) == no2blk.end())
                continue;
            func->getBlocks().push_back(tmp_blks[i]);
            no2blk.erase(tmp_blks[i]->getNo());
            auto lastInst = tmp_blks[i]->getInsts().back();
            if (lastInst->isBranch() && dynamic_cast<BranchMInstruction*>(lastInst)->getOpType() == BranchMInstruction::B)
            {
                auto label = lastInst->getUse()[0]->getLabel();
                label = label.substr(2, label.size() - 2);
                auto succNo = atoi(label.c_str());
                if (no2blk.find(succNo) != no2blk.end())
                {
                    func->getBlocks().push_back(no2blk[succNo]);
                    no2blk.erase(succNo);
                    tmp_blks[i]->removeInst(lastInst);
                }
            }
        }
    }
    for (auto func : unit->getFuncs())
    {
        for (int i = 0; i < func->getBlocks().size() - 1; i++)
        {
            auto blk = func->getBlocks()[i];
            auto lastInst = blk->getInsts().back();
            if (lastInst->isBranch() && dynamic_cast<BranchMInstruction*>(lastInst)->getOpType() == BranchMInstruction::B)
            {
                auto label = lastInst->getUse()[0]->getLabel();
                label = label.substr(2, label.size() - 2);
                auto succNo = atoi(label.c_str());
                if (succNo == func->getBlocks()[i + 1]->getNo())
                    blk->removeInst(lastInst);
            }
        }
    }
}
