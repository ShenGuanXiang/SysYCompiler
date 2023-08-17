#include "LoopAnalyzer.h"

std::set<BasicBlock *> LoopAnalyzer::computeNaturalLoop(BasicBlock *exit_bb, BasicBlock *header_bb)
{
    std::set<BasicBlock *> loop_bbs;
    std::queue<BasicBlock *> q1, q2;
    std::set<BasicBlock *> from_header, from_exit;

    assert(exit_bb != nullptr && header_bb != nullptr);
    if (exit_bb == header_bb)
    {
        loop_bbs.insert(exit_bb);
        return loop_bbs;
    }

    q1.push(header_bb);
    q2.push(exit_bb);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b++)
            if (from_header.find(*b) == from_header.end() && (*b) != exit_bb)
            {
                q1.push(*b);
                from_header.insert(*b);
                // fprintf(stderr, "from_header : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    while (!q2.empty())
    {
        auto t = q2.front();
        q2.pop();
        for (auto b = t->pred_begin(); b != t->pred_end(); b++)
            if (from_exit.find(*b) == from_exit.end() && (*b) != header_bb)
            {
                q2.push(*b);
                from_exit.insert(*b);
                // fprintf(stderr, "from_exit : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    std::set_intersection(from_header.begin(), from_header.end(), from_exit.begin(), from_exit.end(), std::inserter(loop_bbs, loop_bbs.end()));
    loop_bbs.insert(header_bb);
    loop_bbs.insert(exit_bb);

    return loop_bbs;
}

void LoopAnalyzer::getBackEdges()
{
    func->ComputeDom();
    std::queue<BasicBlock *> q;
    q.push(func->getEntry());
    std::set<BasicBlock *> visited;
    visited.insert(func->getEntry());
    while (!q.empty())
    {
        auto t = q.front();
        q.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b++)
        {
            auto sDoms = t->getSDoms();
            if (*b == t || sDoms.find(*b) != sDoms.end())
            {
                backEdges.insert({t, *b});
                // fprintf(stderr, "Back_Edge from %d to %d\n", t->getNo(), (*b)->getNo());
            }
            if (visited.find(*b) == visited.end())
                q.push(*b), visited.insert(*b);
        }
    }
}

bool LoopAnalyzer::isSubset(std::set<BasicBlock *> t_son, std::set<BasicBlock *> t_fat)
{
    for (auto s : t_son)
        if (t_fat.find(s) == t_fat.end())
            return false;

    return t_son.size() != t_fat.size();
}

void LoopAnalyzer::analyze()
{
    backEdges.clear();
    loopDepthMap.clear();
    loops.clear();

    getBackEdges();
    for (auto bb : func->getBlockList())
        loopDepthMap[bb] = 0;
    for (auto [exit_bb, header_bb] : backEdges)
    {
        auto loop_bbs = computeNaturalLoop(exit_bb, header_bb);
        auto loop = new Loop(loop_bbs, 0, true, true, header_bb, exit_bb);
        loops.insert(loop);
        for (auto &bb : loop_bbs)
            loopDepthMap[bb]++;
    }
    for (auto loop : loops)
    {
        loop->loop_depth = 0x3fffffff;
        for (auto bb : loop->loop_bbs)
            loop->loop_depth = std::min(loopDepthMap[bb], loop->loop_depth);
    }

    for (auto loop : getLoops())
    {
        loop->isInnerLoop = true;
        loop->isOuterLoop = true;
    }

    for (auto loop1 : getLoops())
        for (auto loop2 : getLoops())
            if (isSubset(loop1->loop_bbs, loop2->loop_bbs))
            {
                loop1->isOuterLoop = false;
                loop2->isInnerLoop = false;
            }
}

std::set<MachineBlock *> MLoopAnalyzer::computeNaturalLoop(MachineBlock *exit_bb, MachineBlock *header_bb)
{
    std::set<MachineBlock *> loop_bbs;
    std::queue<MachineBlock *> q1, q2;
    std::set<MachineBlock *> from_header, from_exit;

    assert(exit_bb != nullptr && header_bb != nullptr);
    if (exit_bb == header_bb)
    {
        loop_bbs.insert(exit_bb);
        return loop_bbs;
    }

    q1.push(header_bb);
    q2.push(exit_bb);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b : t->getSuccs())
            if (from_header.find(b) == from_header.end() && b != exit_bb)
            {
                q1.push(b);
                from_header.insert(b);
            }
    }
    while (!q2.empty())
    {
        auto t = q2.front();
        q2.pop();
        for (auto b : t->getPreds())
            if (from_exit.find(b) == from_exit.end() && b != header_bb)
            {
                q2.push(b);
                from_exit.insert(b);
            }
    }
    std::set_intersection(from_header.begin(), from_header.end(), from_exit.begin(), from_exit.end(), std::inserter(loop_bbs, loop_bbs.end()));
    loop_bbs.insert(header_bb);
    loop_bbs.insert(exit_bb);

    return loop_bbs;
}

void MLoopAnalyzer::getBackEdges()
{
    func->computeDom();
    std::queue<MachineBlock *> q;
    q.push(func->getEntry());
    std::set<MachineBlock *> visited;
    visited.insert(func->getEntry());
    while (!q.empty())
    {
        auto t = q.front();
        q.pop();
        for (auto b : t->getSuccs())
        {
            auto sDoms = t->getSDoms();
            if (b == t || sDoms.find(b) != sDoms.end())
            {
                backEdges.insert({t, b});
                // fprintf(stderr, "M_Back_Edge from %d to %d\n", t->getNo(), (*b)->getNo());
            }
            if (visited.find(b) == visited.end())
                q.push(b), visited.insert(b);
        }
    }
}

void MLoopAnalyzer::analyze()
{
    backEdges.clear();
    loopDepthMap.clear();
    loops.clear();

    getBackEdges();
    for (auto bb : func->getBlocks())
        loopDepthMap[bb] = 0;
    for (auto [exit_bb, header_bb] : backEdges)
    {
        auto loop_bbs = computeNaturalLoop(exit_bb, header_bb);
        auto loop = new MLoop(loop_bbs, header_bb, exit_bb);
        loops.insert(loop);
        for (auto &bb : loop_bbs)
            loopDepthMap[bb]++;
    }
}