#include "LoopAnalyzer.h"

std::set<BasicBlock *> LoopAnalyzer::computeNaturalLoop(BasicBlock *exiting_bb, BasicBlock *header_bb)
{
    std::set<BasicBlock *> loop_bbs;
    std::queue<BasicBlock *> q1, q2;
    std::set<BasicBlock *> from_header, from_exit;

    assert(exiting_bb != nullptr && header_bb != nullptr);
    if (exiting_bb == header_bb)
    {
        loop_bbs.insert(exiting_bb);
        return loop_bbs;
    }

    q1.push(header_bb);
    q2.push(exiting_bb);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b++)
            if (from_header.find(*b) == from_header.end() && (*b) != exiting_bb)
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
    loop_bbs.insert(exiting_bb);

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
                fprintf(stderr, "Back_Edge from %d to %d\n", t->getNo(), (*b)->getNo());
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

std::set<BasicBlock *> LoopAnalyzer::getIntersection(std::set<BasicBlock *> loop1, std::set<BasicBlock *> loop2)
{
    std::set<BasicBlock *> res;
    std::set_intersection(loop1.begin(), loop1.end(), loop2.begin(), loop2.end(), std::inserter(res, res.end()));
    return res;
}

void LoopAnalyzer::computeInductionVars(Loop *loop)
{
    for (auto phi = loop->header_bb->begin(); phi != loop->header_bb->end() && phi->isPHI(); phi = phi->getNext())
    {
        std::vector<std::vector<Instruction *>> du_chains;
        std::queue<std::pair<Instruction *, std::vector<Instruction *>>> q;
        q.push({phi, {}});
        while (!q.empty())
        {
            auto [inst, path] = q.front();
            q.pop();
            if (inst->hasNoDef())
                continue;
            path.push_back(inst);
            for (auto next_inst : inst->getDef()->getUses())
            {
                if (next_inst == phi)
                    du_chains.push_back(path);
                if (find(path.begin(), path.end(), next_inst) == path.end() && loop->loop_bbs.count(next_inst->getParent()))
                    q.push({next_inst, path});
            }
        }
        if (!du_chains.empty())
        {
            loop->inductionVars.insert(new InductionVar(du_chains)); // TODO：路径中的phi（除了起点）有不确定的来源，那么归纳变量是不可计算的
        }
    }
}

bool LoopAnalyzer::multiOr(Loop *loop)
{
    return loop->header_bb == func->getEntry() || loop->header_bb->getNumOfPred() > 2; // 很不精确
}

bool LoopAnalyzer::hasLeak(Loop *loop)
{
    for (auto bb : loop->loop_bbs)
    {
        if (bb == loop->exiting_bb)
            continue;
        for (auto succ_it = bb->succ_begin(); succ_it != bb->succ_end(); succ_it++)
        {
            if (!loop->loop_bbs.count(*succ_it))
            {
                return true;
            }
        }
    }
    return false;
}

int LoopAnalyzer::getLoopCnt(Loop *loop)
{
    int cnt = 1;
    for (auto l : loop->subLoops)
        cnt += getLoopCnt(l);
    return cnt;
}

void LoopAnalyzer::analyze()
{
    backEdges.clear();
    loopDepthMap.clear();
    loops.clear();

    getBackEdges();

    // 先计算所有back edge产生的natural loop
    std::set<Loop *> temp_loops; // 包含冗余loop
    for (auto [exiting_bb, header_bb] : backEdges)
    {
        auto loop_bbs = computeNaturalLoop(exiting_bb, header_bb);
        auto loop = new Loop(loop_bbs, header_bb, exiting_bb);
        temp_loops.insert(loop);
    }
    // 排除冗余loop
    std::set<Loop *> redundant_loops;
    for (auto loop1 : temp_loops)
    {
        for (auto loop2 : temp_loops)
        {
            if (loop2 == loop1)
                continue;
            if (!getIntersection(loop1->loop_bbs, loop2->loop_bbs).empty() && !isSubset(loop2->loop_bbs, loop1->loop_bbs) &&
                (!loop2->loop_bbs.count(loop1->header_bb) || loop1->header_bb == loop2->header_bb))
            {
                redundant_loops.insert(loop1);
                break;
            }
        }
        if (!redundant_loops.count(loop1))
            loops.insert(loop1);
    }

    // 识别 || && 等条件块
    // for (auto loop : loops)
    //     computeLoopCond(loop);

    // 验证
    for (auto loop1 : loops)
        for (auto loop2 : loops)
            if (!getIntersection(loop1->loop_bbs, loop2->loop_bbs).empty())
                assert(loop1 == loop2 || isSubset(loop2->loop_bbs, loop1->loop_bbs) || isSubset(loop1->loop_bbs, loop2->loop_bbs));

    // 计算loop depth
    for (auto bb : func->getBlockList())
        loopDepthMap[bb] = 0;
    for (auto loop : loops)
        for (auto bb : loop->loop_bbs)
            loopDepthMap[bb]++;
    for (auto loop : loops)
    {
        loop->loop_depth = 0x3fffffff;
        for (auto bb : loop->loop_bbs)
            loop->loop_depth = std::min(loopDepthMap[bb], loop->loop_depth);
    }

    // 识别归纳变量
    for (auto loop : loops)
        computeInductionVars(loop);

    // 计算loop嵌套关系
    for (auto loop1 : loops)
        for (auto loop2 : loops)
            if (isSubset(loop1->loop_bbs, loop2->loop_bbs) && loop1->loop_depth == loop2->loop_depth + 1)
            {
                loop1->parentLoop = loop2;
                loop2->subLoops.insert(loop1);
            }

    for (auto loop : loops)
        loop->printLoop();
}

std::set<MachineBlock *> MLoopAnalyzer::computeNaturalLoop(MachineBlock *exiting_bb, MachineBlock *header_bb)
{
    std::set<MachineBlock *> loop_bbs;
    std::queue<MachineBlock *> q1, q2;
    std::set<MachineBlock *> from_header, from_exit;

    assert(exiting_bb != nullptr && header_bb != nullptr);
    if (exiting_bb == header_bb)
    {
        loop_bbs.insert(exiting_bb);
        return loop_bbs;
    }

    q1.push(header_bb);
    q2.push(exiting_bb);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b : t->getSuccs())
            if (from_header.find(b) == from_header.end() && b != exiting_bb)
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
    loop_bbs.insert(exiting_bb);

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

bool MLoopAnalyzer::isSubset(std::set<MachineBlock *> t_son, std::set<MachineBlock *> t_fat)
{
    for (auto s : t_son)
        if (t_fat.find(s) == t_fat.end())
            return false;

    return t_son.size() != t_fat.size();
}

std::set<MachineBlock *> MLoopAnalyzer::getIntersection(std::set<MachineBlock *> loop1, std::set<MachineBlock *> loop2)
{
    std::set<MachineBlock *> res;
    std::set_intersection(loop1.begin(), loop1.end(), loop2.begin(), loop2.end(), std::inserter(res, res.end()));
    return res;
}

void MLoopAnalyzer::analyze()
{
    backEdges.clear();
    loopDepthMap.clear();
    loops.clear();

    getBackEdges();
    // 先计算所有back edge产生的natural loop
    std::set<MLoop *> temp_loops; // 包含冗余loop
    for (auto [exiting_bb, header_bb] : backEdges)
    {
        auto loop_bbs = computeNaturalLoop(exiting_bb, header_bb);
        auto loop = new MLoop(loop_bbs, header_bb, exiting_bb);
        temp_loops.insert(loop);
    }
    // 排除冗余loop
    std::set<MLoop *> redundant_loops;
    for (auto loop1 : temp_loops)
    {
        for (auto loop2 : temp_loops)
        {
            if (loop2 == loop1)
                continue;
            if (!getIntersection(loop1->loop_bbs, loop2->loop_bbs).empty() && !isSubset(loop2->loop_bbs, loop1->loop_bbs) &&
                (!loop2->loop_bbs.count(loop1->header_bb) || loop1->header_bb == loop2->header_bb))
            {
                redundant_loops.insert(loop1);
                break;
            }
        }
        if (!redundant_loops.count(loop1))
            loops.insert(loop1);
    }
    // 验证
    for (auto loop1 : loops)
        for (auto loop2 : loops)
            if (!getIntersection(loop1->loop_bbs, loop2->loop_bbs).empty())
                assert(loop1 == loop2 || isSubset(loop2->loop_bbs, loop1->loop_bbs) || isSubset(loop1->loop_bbs, loop2->loop_bbs));
    for (auto bb : func->getBlocks())
        loopDepthMap[bb] = 0;
    for (auto loop : loops)
        for (auto bb : loop->loop_bbs)
            loopDepthMap[bb]++;
}