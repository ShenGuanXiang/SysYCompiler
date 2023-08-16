#include "LoopUnroll.h"

static Operand *copyOperand(Operand *ope)
{
    if (ope->getEntry()->getType()->isConst())
        return new Operand(new ConstantSymbolEntry(ope->getType(), ope->getEntry()->getValue()));
    else
        return new Operand(new TemporarySymbolEntry(ope->getType(), SymbolTable::getLabel()));
}

void Loop::PrintInfo()
{
    // for (auto b : bb)
    //     fprintf(stderr, "BasicBlock[%d] ", b->getNo());

    // fprintf(stderr, "\nisInerloop: %d\n", InnerLoop);
    // fprintf(stderr, "loopdepth: %d\n", loop_depth);
}

Loop::Loop(std::set<BasicBlock *> origin_bb)
{
    bb.clear();
    for (auto b : origin_bb)
        bb.insert(b);
}

void LoopStruct::PrintInfo()
{
    // fprintf(stderr, "The loopstruct contains basicblock: ");
    // origin_loop->PrintInfo();

    // fprintf(stderr, "cond bb is %d\n", loopstruct.first->getNo());
    // fprintf(stderr, "body bb is %d\n", loopstruct.second->getNo());
}

void LoopAnalyzer::Analyze(Function *f)
{
    edgeType.clear();
    loopDepth.clear();
    visit.clear();
    Loops.clear();

    for (auto &bb : f->getBlockList())
        loopDepth[bb] = 0;

    f->ComputeDom();
    Get_REdge(f->getEntry());
    computeLoopDepth();
}

void LoopAnalyzer::Get_REdge(BasicBlock *root)
{
    std::queue<BasicBlock *> q_edge;
    q_edge.push(root);
    visit.insert(root);
    while (!q_edge.empty())
    {
        auto t = q_edge.front();
        q_edge.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b++)
        {
            auto Dom = t->getSDoms();
            if (*b == t || Dom.find(*b) != Dom.end())
            {
                edgeType.insert({t, *b});
                // fprintf(stderr, "Reverse_Edge from %d to %d\n", t->getNo(), (*b)->getNo());
            }
            if (visit.find(*b) == visit.end())
                q_edge.push(*b), visit.insert(*b);
        }
    }
}

std::set<BasicBlock *> LoopAnalyzer::computeNaturalLoop(BasicBlock *cond, BasicBlock *body)
{
    std::set<BasicBlock *> loop;
    std::queue<BasicBlock *> q1, q2;
    std::set<BasicBlock *> from_body, from_cond;

    assert(cond != nullptr && body != nullptr);
    if (cond == body)
    {
        loop.insert(cond);
        return loop;
    }

    q1.push(body);
    q2.push(cond);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b = t->succ_begin(); b != t->succ_end(); b++)
            if (from_body.find(*b) == from_body.end() && (*b) != cond)
            {
                q1.push(*b);
                from_body.insert(*b);
                // fprintf(stderr, "from_body : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    while (!q2.empty())
    {
        auto t = q2.front();
        q2.pop();
        for (auto b = t->pred_begin(); b != t->pred_end(); b++)
            if (from_cond.find(*b) == from_cond.end() && (*b) != body)
            {
                q2.push(*b);
                from_cond.insert(*b);
                // fprintf(stderr, "from_cond : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    std::set_intersection(from_body.begin(), from_body.end(), from_cond.begin(), from_cond.end(), std::inserter(loop, loop.end()));
    loop.insert(body);
    loop.insert(cond);

    // for (auto l : loop)
    //     fprintf(stderr, "bb[%d] in loop\n", l->getNo());
    // fprintf(stderr, "compute finish\n");
    return loop;
}

void LoopAnalyzer::computeLoopDepth()
{
    for (auto edge : edgeType)
    {
        auto loop = new Loop(computeNaturalLoop(edge.first, edge.second));
        auto loopstruct = new LoopStruct(loop);
        loopstruct->SetBody(edge.second);
        loopstruct->SetCond(edge.first);
        Loops.insert(loopstruct);
        for (auto &b : loop->GetBasicBlock())
        {
            loopDepth[b]++;
            // fprintf(stderr, "loop[%d] depth is %d\n", b->getNo(), loopDepth[b]);
        }
        // fprintf(stderr, "loop End\n");
    }
}

bool LoopAnalyzer::isSubset(std::set<BasicBlock *> t_son, std::set<BasicBlock *> t_fat)
{
    for (auto s : t_son)
        if (t_fat.find(s) == t_fat.end())
            return false;

    return t_son.size() != t_fat.size();
}

void LoopAnalyzer::FindLoops(Function *f)
{
    Analyze(f);
    for (auto l : getLoops())
    {
        l->GetLoop()->SetDepth(0x3fffffff);
        // for (auto bb : l->GetLoop()->GetBasicBlock())
        //     fprintf(stderr, "bb[%d]'s depth is %d\n", bb->getNo(),
        //     getLoopDepth(bb));
    }

    for (auto l : getLoops())
    {
        l->GetLoop()->SetDepth(0x3fffffff);
        for (auto bb : l->GetLoop()->GetBasicBlock())
            l->GetLoop()->SetDepth(std::min(getLoopDepth(bb), l->GetLoop()->GetDepth()));
    }

    for (auto l : getLoops())
        l->GetLoop()->SetInnerLoop();

    for (auto l1 : getLoops())
        for (auto l2 : getLoops())
            if (isSubset(l1->GetLoop()->GetBasicBlock(), l2->GetLoop()->GetBasicBlock()))
                l2->GetLoop()->ClearInnerLoop();
}

void LoopAnalyzer::PrintInfo(Function *f)
{
    std::string FunctionName = ((IdentifierSymbolEntry *)f->getSymPtr())->getName();
    fprintf(stderr, "Function %s's Loop Analysis :\n", FunctionName.c_str());
    for (auto l : Loops)
        l->PrintInfo();

    fprintf(stderr, "\n");
}

static MachineOperand *copyOperand(MachineOperand *ope)
{
    return new MachineOperand(*ope);
}

void MLoop::PrintInfo()
{
    for (auto b : bb)
        fprintf(stderr, "BasicBlock[%d] ", b->getNo());

    fprintf(stderr, "\nisInerloop: %d\n", InnerLoop);
    fprintf(stderr, "loopdepth: %d\n", loop_depth);
}

MLoop::MLoop(std::set<MachineBlock *> origin_bb)
{
    bb.clear();
    for (auto b : origin_bb)
        bb.insert(b);
}

void MLoopStruct::PrintInfo()
{
    fprintf(stderr, "The loopstruct contains basicblock: ");
    origin_loop->PrintInfo();

    fprintf(stderr, "cond bb is %d\n", loopstruct.first->getNo());
    fprintf(stderr, "body bb is %d\n", loopstruct.second->getNo());
}

void MLoopAnalyzer::Analyze(MachineFunction *f)
{
    edgeType.clear();
    loopDepth.clear();
    visit.clear();
    Loops.clear();

    for (auto &bb : f->getBlocks())
        loopDepth[bb] = 0;

    f->computeDom();
    Get_REdge(f->getEntry());
    computeLoopDepth();
}

void MLoopAnalyzer::Get_REdge(MachineBlock *root)
{
    std::queue<MachineBlock *> q_edge;
    q_edge.push(root);
    visit.insert(root);
    while (!q_edge.empty())
    {
        auto t = q_edge.front();
        q_edge.pop();
        auto vec = t->getSuccs();
        for (auto b : vec)
        {
            auto Dom = t->getSDoms();
            if (b == t || Dom.find(b) != Dom.end())
            {
                edgeType.insert({t, b});
                // fprintf(stderr, "M_Reverse_Edge from %d to %d\n", t->getNo(), b->getNo());
            }
            if (visit.find(b) == visit.end())
                q_edge.push(b), visit.insert(b);
        }
    }
}

std::set<MachineBlock *> MLoopAnalyzer::computeNaturalLoop(MachineBlock *cond, MachineBlock *body)
{
    std::set<MachineBlock *> loop;
    std::queue<MachineBlock *> q1, q2;
    std::set<MachineBlock *> from_body, from_cond;

    assert(cond != nullptr && body != nullptr);
    if (cond == body)
    {
        loop.insert(cond);
        return loop;
    }

    q1.push(body);
    q2.push(cond);
    while (!q1.empty())
    {
        auto t = q1.front();
        q1.pop();
        for (auto b : t->getSuccs())
            if (from_body.find(b) == from_body.end() && b != cond)
            {
                q1.push(b);
                from_body.insert(b);
                // fprintf(stderr, "from_body : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    while (!q2.empty())
    {
        auto t = q2.front();
        q2.pop();
        for (auto b : t->getPreds())
            if (from_cond.find(b) == from_cond.end() && b != body)
            {
                q2.push(b);
                from_cond.insert(b);
                // fprintf(stderr, "from_cond : bb[%d] from bb[%d] in loop\n", (*b)->getNo(), t->getNo());
            }
    }
    std::set_intersection(from_body.begin(), from_body.end(), from_cond.begin(), from_cond.end(), std::inserter(loop, loop.end()));
    loop.insert(body);
    loop.insert(cond);

    // for (auto l : loop)
    //     fprintf(stderr, "bb[%d] in loop\n", l->getNo());
    // fprintf(stderr, "compute finish\n");
    return loop;
}

void MLoopAnalyzer::computeLoopDepth()
{
    for (auto edge : edgeType)
    {
        auto loop = new MLoop(computeNaturalLoop(edge.first, edge.second));
        auto loopstruct = new MLoopStruct(loop);
        loopstruct->SetBody(edge.second);
        loopstruct->SetCond(edge.first);
        Loops.insert(loopstruct);
        for (auto &b : loop->GetBasicBlock())
        {
            loopDepth[b]++;
            // fprintf(stderr, "loop[%d] depth is %d\n", b->getNo(), loopDepth[b]);
        }
        // fprintf(stderr, "loop End\n");
    }
}

bool MLoopAnalyzer::isSubset(std::set<MachineBlock *> t_son, std::set<MachineBlock *> t_fat)
{
    for (auto s : t_son)
        if (t_fat.find(s) == t_fat.end())
            return false;

    return t_son.size() != t_fat.size();
}

void MLoopAnalyzer::FindLoops(MachineFunction *f)
{
    Analyze(f);
    for (auto l : getLoops())
    {
        l->GetLoop()->SetDepth(0x3fffffff);
        // for (auto bb : l->GetLoop()->GetBasicBlock())
        //     fprintf(stderr, "bb[%d]'s depth is %d\n", bb->getNo(),
        //     getLoopDepth(bb));
    }

    for (auto l : getLoops())
    {
        l->GetLoop()->SetDepth(0x3fffffff);
        for (auto bb : l->GetLoop()->GetBasicBlock())
            l->GetLoop()->SetDepth(std::min(getLoopDepth(bb), l->GetLoop()->GetDepth()));
    }

    for (auto l : getLoops())
        l->GetLoop()->SetInnerLoop();

    for (auto l1 : getLoops())
        for (auto l2 : getLoops())
            if (isSubset(l1->GetLoop()->GetBasicBlock(), l2->GetLoop()->GetBasicBlock()))
                l2->GetLoop()->ClearInnerLoop();
}

void MLoopAnalyzer::PrintInfo(MachineFunction *f)
{
    std::string FunctionName = ((IdentifierSymbolEntry *)f->getSymPtr())->getName();
    fprintf(stderr, "Function %s's Loop Analysis :\n", FunctionName.c_str());
    for (auto l : Loops)
        l->PrintInfo();

    fprintf(stderr, "\n");
}

void LoopUnroll::pass()
{
    for (auto f : unit->getFuncList())
    {
        analyzer.FindLoops(f);
        Loops = analyzer.getLoops();
        auto cand = FindCandidateLoop();
        for (auto candidate : cand)
            Unroll(candidate);
    }
}

std::vector<LoopStruct *> LoopUnroll::FindCandidateLoop()
{
    // std::vector<LoopStruct*> Worklist;
    // for (auto f : unit->getFuncList()) {
    //     for(auto loop : analyzer.FindLoops(f)){

    //         // find cond and body
    //         BasicBlock* cond = nullptr, *body = nullptr;
    //         for(auto bb : loop->getbbs()){
    //             for(auto instr = bb->begin(); instr != bb->end()->getNext(); instr = instr->getNext()){
    //                 if(instr->isCmp()){
    //                     cond = bb;
    //                     break;
    //                 }
    //             }
    //             if(cond) break;
    //         }

    //         for(auto bb : loop->getbbs()){
    //             if(bb != cond){
    //                 body = bb;
    //             }
    //         }

    //         LoopStruct* CandidateLoop = new LoopStruct(loop);
    //         CandidateLoop->SetCond(cond);
    //         CandidateLoop->SetBody(body);
    //         Worklist.push_back(CandidateLoop);
    //     }
    // }

    // for(auto CandidateLoop : Worklist){
    //     bool HasCallInBody = false;
    //     for(auto bodyinstr = CandidateLoop->getBody()->begin(); bodyinstr != CandidateLoop->getBody()->end()->getNext(); bodyinstr = bodyinstr->getNext()){
    //         if(bodyinstr->isCall()){
    //             HasCallInBody = true;
    //             break;
    //         }
    //     }
    //     if(HasCallInBody){
    //         Exception("Candidate loop shall have no call in body");
    //         continue;
    //     }

    //     bool CheckFlag = false;
    //     for(auto bb = CandidateLoop->getCond()->succ_begin(); bb != CandidateLoop->getCond()->succ_end(); bb++){
    //         CheckFlag = CheckFlag || (*bb == CandidateLoop->getBody());
    //     }
    //     if(!CheckFlag){
    //         continue;
    //     }
    // }
}

void LoopUnroll::Unroll(LoopStruct *)
{
}
